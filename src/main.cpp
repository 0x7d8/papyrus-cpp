#include <uWebSockets/App.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include <nlohmann/json.hpp>

#include <utils/database.h>
#include <utils/logger.cpp>
#include <utils/storage.cpp>

using json = nlohmann::json;

class Arguments {
private:
	std::map<std::string, std::string> _arguments;
public:
	Arguments(int argc, char *argv[]) {
		for (int i = 1; i < argc; i++) {
			std::string argument = argv[i];
			std::string key = argument.substr(0, argument.find("="));
			std::string value = argument.substr(argument.find("=") + 1);

			_arguments[key] = value;
		}
	}

	std::string get(const std::string &key) {
		return _arguments[key];
	}
};

int main(int argc, char *argv[]) {
	auto arguments = Arguments(argc, argv);
	auto database = DB("database.sqlite");
	auto storage = Storage("storage");
	auto app = uWS::App();

	migrate(database.get());

	auto key = arguments.get("key");
	if (key.empty()) {
		Logger::color(Color::RED).log("Missing key argument");
		return 1;
	}

	auto port = arguments.get("port");
	if (!port.empty()) {
		app.listen(std::stoi(port), [port](auto *token) {
			if (token) {
				Logger::color(Color::GREEN).log("Listening on port " + port);
			}
		});
	} else {
		app.listen(3000, [](auto *token) {
			if (token) {
				Logger::color(Color::GREEN).log("Listening on port 3000");
			}
		});
	}

	app.post("/v2/create", [&database, key](auto *res, auto *req) {
		if (req->getHeader("authorization") != key) {
			res->cork([res]() {
				res->writeStatus("401 Unauthorized");
				res->writeHeader("Content-Type", "application/json");
				res->end("{\"error\": \"Unauthorized\"}");
			});

			return;
		}

		struct RequestContext {
			std::shared_ptr<std::string> body;
			bool closed;

			RequestContext() : body(std::make_shared<std::string>()), closed(false) {}
		};

		std::shared_ptr<RequestContext> context = std::make_shared<RequestContext>();

		res->onAborted([context]() {
			context->closed = true;
		});

		res->onData([&database, res, context](std::string_view chunk, bool last) {
			context->body->append(std::string(chunk));

			if (last) {
				try {
					auto data = json::parse(std::string(context->body->c_str()));

					// validate data
					if (!data.contains("project") || !data.contains("version") || !data.contains("fileExtension") || !data.contains("build") || !data.contains("result") || !data.contains("timestamp") || !data.contains("duration") || !data.contains("commits") || !data.contains("metadata")) {
						if (context->closed) {
							return;
						}

						res->cork([res]() {
							res->writeStatus("400 Bad Request");
							res->writeHeader("Content-Type", "application/json");
							res->end("{\"error\": \"Missing Required Fields\"}");
						});

						return;
					}

					for (auto commit : data["commits"]) {
						if (!commit.contains("author") || !commit.contains("email") || !commit.contains("description") || !commit.contains("hash") || !commit.contains("timestamp")) {
							if (context->closed) {
								return;
							}

							res->cork([res]() {
								res->writeStatus("400 Bad Request");
								res->writeHeader("Content-Type", "application/json");
								res->end("{\"error\": \"Invalid Commit\"}");
							});

							return;
						}
					}

					SQLite::Statement query(database.get(), "INSERT INTO projects (name) VALUES (?) ON CONFLICT DO NOTHING;");
					query.bind(1, data["project"].get<std::string>());

					database.get().exec(query.getExpandedSQL());

					query = SQLite::Statement(database.get(), "INSERT INTO versions (project_id, name) VALUES ((SELECT id FROM projects WHERE name = ?), ?) ON CONFLICT DO NOTHING;");
					query.bind(1, data["project"].get<std::string>());
					query.bind(2, data["version"].get<std::string>());

					database.get().exec(query.getExpandedSQL());

					query = SQLite::Statement(database.get(), "SELECT id FROM builds WHERE version_id = (SELECT id FROM versions WHERE project_id = (SELECT id FROM projects WHERE name = ?) AND name = ?) AND build = ?;");
					query.bind(1, data["project"].get<std::string>());
					query.bind(2, data["version"].get<std::string>());
					query.bind(3, data["build"].get<std::string>());

					auto results = database.query(query);
					if (results.size()) {
						if (context->closed) {
							return;
						}

						res->cork([res]() {
							res->writeStatus("400 Bad Request");
							res->writeHeader("Content-Type", "application/json");
							res->end("{\"error\": \"Build Already Exists\"}");
						});

						return;
					}

					query = SQLite::Statement(database.get(), "INSERT INTO builds (version_id, ready, file_extension, build, result, timestamp, duration, commits, metadata, md5, sha256, sha512) VALUES ((SELECT id FROM versions WHERE project_id = (SELECT id FROM projects WHERE name = ?) AND name = ?), 0, ?, ?, ?, ?, ?, ?, ?, '', '', '') RETURNING id;");
					query.bind(1, data["project"].get<std::string>());
					query.bind(2, data["version"].get<std::string>());
					query.bind(3, data["fileExtension"].get<std::string>());
					query.bind(4, data["build"].get<std::string>());
					query.bind(5, data["result"].get<std::string>());
					query.bind(6, data["timestamp"].get<long>());
					query.bind(7, data["duration"].get<int>());
					query.bind(8, data["commits"].dump());
					query.bind(9, data["metadata"].dump());

					auto insert = database.query(query);
					if (!insert.size()) {
						if (context->closed) {
							return;
						}

						res->cork([res]() {
							res->writeStatus("500 Internal Server Error");
							res->writeHeader("Content-Type", "application/json");
							res->end("{\"error\": \"Failed to Create Build\"}");
						});

						return;
					}

					auto build = insert.front();

					if (context->closed) {
						return;
					}

					auto json = json::object();

					json["id"] = build["id"];

					res->cork([res, &json]() {
						res->writeHeader("Content-Type", "application/json");
						res->end(json.dump());
					});
				} catch (json::parse_error &e) {
					if (context->closed) {
						return;
					}

					res->cork([res]() {
						res->writeStatus("400 Bad Request");
						res->writeHeader("Content-Type", "application/json");
						res->end("{\"error\": \"Invalid JSON\"}");
					});
				}
			}
		});
	});

	app.post("/v2/create/upload/:build", [&database, &storage, key](auto *res, auto *req) {
		if (req->getHeader("authorization") != key) {
			res->cork([res]() {
				res->writeStatus("401 Unauthorized");
				res->writeHeader("Content-Type", "application/json");
				res->end("{\"error\": \"Unauthorized\"}");
			});

			return;
		}

		std::string build = std::string(req->getParameter(0)).data();

		if (build.find_first_not_of("0123456789") != std::string::npos) {
			res->cork([res]() {
				res->writeStatus("400 Bad Request");
				res->writeHeader("Content-Type", "application/json");
				res->end("{\"error\": \"Invalid Build\"}");
			});

			return;
		}

		int buildId = std::stoi(build);

		SQLite::Statement query(database.get(), "SELECT md5 FROM builds WHERE id = ?;");
		query.bind(1, buildId);

		auto results = database.query(query);

		if (!results.size()) {
			res->cork([res]() {
				res->writeStatus("404 Not Found");
				res->writeHeader("Content-Type", "application/json");
				res->end("{\"error\": \"Build Not Found\"}");
			});

			return;
		}

		auto result = results.front();

		if (!result["md5"].empty()) {
			storage.remove(result["md5"]);
		}

		auto stream = storage.store(build);

		if (!stream.is_open()) {
			res->cork([res]() {
				res->writeStatus("500 Internal Server Error");
				res->writeHeader("Content-Type", "application/json");
				res->end("{\"error\": \"Failed to Store Build\"}");
			});

			return;
		}

		struct RequestContext {
			int buildId;
			bool closed;
			std::ofstream stream;

			RequestContext() : buildId(0), closed(false), stream() {}
		};

		std::shared_ptr<RequestContext> context = std::make_shared<RequestContext>();

		context->buildId = buildId;
		context->stream = std::move(stream);

		res->onAborted([context]() {
			context->closed = true;
			context->stream.close();
		});

		res->onData([&database, &storage, res, context](std::string_view chunk, bool last) {
			context->stream << chunk;

			if (last) {
				context->stream.close();

				auto hashes = storage.finalize(std::to_string(context->buildId));

				SQLite::Statement query(database.get(), "UPDATE builds SET ready = 1, md5 = ?, sha256 = ?, sha512 = ? WHERE id = ?;");
				query.bind(1, hashes["md5"]);
				query.bind(2, hashes["sha256"]);
				query.bind(3, hashes["sha512"]);
				query.bind(4, context->buildId);

				database.get().exec(query.getExpandedSQL());

				auto json = json::object();

				json["md5"] = hashes["md5"];
				json["sha256"] = hashes["sha256"];
				json["sha512"] = hashes["sha512"];

				res->cork([res, &json]() {
					res->writeHeader("Content-Type", "application/json");
					res->end(json.dump());
				});
			}
		});
	});

	app.get("/v2", [&database](auto *res, auto *req) {
		SQLite::Statement query(database.get(), "SELECT name FROM projects");
		auto json = json::object();

		json["projects"] = json::array();

		for (auto row : database.query(query)) {
			json["projects"].push_back(row["name"]);
		}

		res->cork([res, &json]() {
			res->writeHeader("Content-Type", "application/json");
			res->end(json.dump());
		});
	});

	app.get("/v2/:project", [&database](auto *res, auto *req) {
		std::string project = std::string(req->getParameter(0)).data();

		SQLite::Statement query(database.get(), "SELECT name FROM versions WHERE project_id = (SELECT id FROM projects WHERE name = ?)");
		query.bind(1, project);

		auto json = json::object();

		json["project"] = project;
		json["versions"] = json::array();

		for (auto row : database.query(query)) {
			json["versions"].push_back(row["name"]);
		}

		res->cork([res, &json]() {
			res->writeHeader("Content-Type", "application/json");
			res->end(json.dump());
		});
	});

	app.get("/v2/:project/:version", [&database](auto *res, auto *req) {
		std::string project = std::string(req->getParameter(0)).data();
		std::string version = std::string(req->getParameter(1)).data();

		SQLite::Statement query(database.get(), "SELECT * FROM builds WHERE version_id = (SELECT id FROM versions WHERE project_id = (SELECT id FROM projects WHERE name = ?) AND name = ?) AND ready = 1 ORDER BY id ASC");
		query.bind(1, project);
		query.bind(2, version);

		auto json = json::object();

		json["project"] = project;
		json["version"] = version;

		json["builds"] = json::object();
		json["builds"]["latest"] = json::object();
		json["builds"]["all"] = json::array();

		auto results = database.query(query);

		if (!results.size()) {
			res->cork([res]() {
				res->writeStatus("404 Not Found");
				res->writeHeader("Content-Type", "application/json");
				res->end("{\"error\": \"Version Not Found\"}");
			});

			return;
		}

		auto latest = results.back();

		json["builds"]["latest"]["project"] = project;
		json["builds"]["latest"]["version"] = version;
		json["builds"]["latest"]["build"] = latest["build"];
		json["builds"]["latest"]["result"] = latest["result"];
		json["builds"]["latest"]["timestamp"] = std::stol(latest["timestamp"]);
		json["builds"]["latest"]["duration"] = std::stoi(latest["duration"]);
		json["builds"]["latest"]["md5"] = latest["md5"];
		json["builds"]["latest"]["sha256"] = latest["sha256"];
		json["builds"]["latest"]["sha512"] = latest["sha512"];
		json["builds"]["latest"]["commits"] = json::parse(latest["commits"]);
		json["builds"]["latest"]["metadata"] = json::parse(latest["metadata"]);

		for (auto row : results) {
			auto build = json::object();

			build["project"] = project;
			build["version"] = version;
			build["build"] = row["build"];
			build["result"] = row["result"];
			build["timestamp"] = std::stol(row["timestamp"]);
			build["duration"] = std::stoi(row["duration"]);
			build["md5"] = row["md5"];
			build["sha256"] = row["sha256"];
			build["sha512"] = row["sha512"];
			build["commits"] = json::parse(row["commits"]);
			build["metadata"] = json::parse(row["metadata"]);

			json["builds"]["all"].push_back(build);
		}

		res->cork([res, &json]() {
			res->writeHeader("Content-Type", "application/json");
			res->end(json.dump());
		});
	});

	app.get("/v2/:project/:version/:build", [&database](auto *res, auto *req) {
		std::string project = std::string(req->getParameter(0)).data();
		std::string version = std::string(req->getParameter(1)).data();
		std::string build = std::string(req->getParameter(2)).data();

		SQLite::Statement query(database.get(), "SELECT * FROM builds WHERE version_id = (SELECT id FROM versions WHERE project_id = (SELECT id FROM projects WHERE name = ?) AND name = ?) AND (build = ? OR ? = 'latest') AND ready = 1 ORDER BY id DESC LIMIT 1");
		query.bind(1, project);
		query.bind(2, version);
		query.bind(3, build);
		query.bind(4, build);

		auto json = json::object();

		json["project"] = project;
		json["version"] = version;

		auto results = database.query(query);

		if (!results.size()) {
			res->cork([res]() {
				res->writeStatus("404 Not Found");
				res->writeHeader("Content-Type", "application/json");
				res->end("{\"error\": \"Build Not Found\"}");
			});

			return;
		}

		auto row = results.front();

		json["build"] = row["build"];
		json["result"] = row["result"];
		json["timestamp"] = std::stol(row["timestamp"]);
		json["duration"] = std::stoi(row["duration"]);
		json["md5"] = row["md5"];
		json["sha256"] = row["sha256"];
		json["sha512"] = row["sha512"];
		json["commits"] = json::parse(row["commits"]);
		json["metadata"] = json::parse(row["metadata"]);

		res->cork([res, &json]() {
			res->writeHeader("Content-Type", "application/json");
			res->end(json.dump());
		});
	});

	app.put("/v2/:project/:version/:build/metadata", [&database, key](auto *res, auto *req) {
		if (req->getHeader("authorization") != key) {
			res->cork([res]() {
				res->writeStatus("401 Unauthorized");
				res->writeHeader("Content-Type", "application/json");
				res->end("{\"error\": \"Unauthorized\"}");
			});

			return;
		}

		std::string project = std::string(req->getParameter(0)).data();
		std::string version = std::string(req->getParameter(1)).data();
		std::string build = std::string(req->getParameter(2)).data();

		SQLite::Statement query(database.get(), "SELECT id FROM builds WHERE version_id = (SELECT id FROM versions WHERE project_id = (SELECT id FROM projects WHERE name = ?) AND name = ?) AND build = ? AND ready = 1 ORDER BY id DESC LIMIT 1");
		query.bind(1, project);
		query.bind(2, version);
		query.bind(3, build);

		auto results = database.query(query);

		if (!results.size()) {
			res->cork([res]() {
				res->writeStatus("404 Not Found");
				res->writeHeader("Content-Type", "application/json");
				res->end("{\"error\": \"Build Not Found\"}");
			});

			return;
		}

		auto buildId = results.front()["id"];

		struct RequestContext {
			std::shared_ptr<std::string> body;
			bool closed;

			RequestContext() : body(std::make_shared<std::string>()), closed(false) {}
		};

		std::shared_ptr<RequestContext> context = std::make_shared<RequestContext>();

		res->onAborted([context]() {
			context->closed = true;
		});

		res->onData([&database, res, context, buildId](std::string_view chunk, bool last) {
			context->body->append(std::string(chunk));

			if (last) {
				try {
					auto metadata = json::parse(std::string(context->body->c_str()));

					SQLite::Statement query(database.get(), "UPDATE builds SET metadata = ? WHERE id = ?;");
					query.bind(1, metadata.dump());
					query.bind(2, buildId);

					database.get().exec(query.getExpandedSQL());

					res->cork([res]() {
						res->writeHeader("Content-Type", "application/json");
						res->end("{\"success\": true}");
					});
				} catch (json::parse_error &e) {
					res->cork([res]() {
						res->writeStatus("400 Bad Request");
						res->writeHeader("Content-Type", "application/json");
						res->end("{\"error\": \"Invalid JSON\"}");
					});
				}
			}
		});
	});

	app.get("/v2/:project/:version/:build/download", [&database, &storage](auto *res, auto *req) {
		std::string project = std::string(req->getParameter(0)).data();
		std::string version = std::string(req->getParameter(1)).data();
		std::string build = std::string(req->getParameter(2)).data();

		SQLite::Statement query(database.get(), "SELECT file_extension, md5, build FROM builds WHERE version_id = (SELECT id FROM versions WHERE project_id = (SELECT id FROM projects WHERE name = ?) AND name = ?) AND (build = ? OR ? = 'latest') AND ready = 1 ORDER BY id DESC LIMIT 1");
		query.bind(1, project);
		query.bind(2, version);
		query.bind(3, build);
		query.bind(4, build);

		auto results = database.query(query);

		if (!results.size()) {
			res->cork([res]() {
				res->writeStatus("404 Not Found");
				res->writeHeader("Content-Type", "application/json");
				res->end("{\"error\": \"Build Not Found\"}");
			});

			return;
		}

		auto row = results.front();

		auto size = storage.size(row["md5"]);
		auto stream = storage.retrieve(row["md5"]);

		if (!stream.is_open()) {
			res->cork([res]() {
				res->writeStatus("500 Internal Server Error");
				res->writeHeader("Content-Type", "application/json");
				res->end("{\"error\": \"Failed to Retrieve Build\"}");
			});

			return;
		}

		res->onAborted([&stream]() {
			stream.close();
		});

		res->cork([res, &stream, project, version, build, &row, size]() {
			res->writeHeader("Content-Type", "application/octet-stream");
			res->writeHeader("Content-Length", std::to_string(size));
			res->writeHeader("Content-Disposition", "attachment; filename=\"" + project + "-" + version + "-" + row["build"] + "." + row["file_extension"] + "\"");
		});

		char buffer[1024];
		bool paused = false;
		std::streampos lastPosition;

		auto readAndProcess = [&]() -> bool {
			if (paused) return false;

			stream.read(buffer, sizeof(buffer));
			std::streamsize bytesRead = stream.gcount();
			
			if (bytesRead == 0) return false;

			auto lastOffset = res->getWriteOffset();
			auto [ok, done] = res->tryEnd(std::string_view(buffer, bytesRead), size);

			if (done) {
				return false;
			} else if (!ok) {
				paused = true;
				lastPosition = stream.tellg();
				res->onWritable([&](int offset) {
					char* sliced = buffer + (offset - lastOffset);
					std::streamsize remainingBytes = bytesRead - (offset - lastOffset);

					auto [ok, done] = res->tryEnd(std::string_view(sliced, remainingBytes), size);
					
					if (done) {
						return false;
					}

					if (ok) {
						paused = false;
						stream.seekg(lastPosition);
					}

					return ok;
				});
			}

			return true;
		};

		while (readAndProcess()) {}

		stream.close();
	});

	app.run();

	return 0;
}