#include <string>
#include <vector>
#include <SQLiteCpp/SQLiteCpp.h>

std::string migrations() {
	return R"(
CREATE TABLE IF NOT EXISTS `builds` (
	`id` integer PRIMARY KEY NOT NULL,
	`version_id` integer NOT NULL,
	`ready` integer NOT NULL,
	`file_extension` text NOT NULL,
	`build` text NOT NULL,
	`result` text NOT NULL,
	`timestamp` integer NOT NULL,
	`duration` integer,
	`md5` text(32) NOT NULL,
	`sha256` text(64) NOT NULL,
	`sha512` text(128) NOT NULL,
	`commits` text NOT NULL,
	`metadata` text NOT NULL
);
--> statement-breakpoint
CREATE TABLE IF NOT EXISTS `projects` (
	`id` integer PRIMARY KEY NOT NULL,
	`name` text NOT NULL
);
--> statement-breakpoint
CREATE TABLE IF NOT EXISTS `versions` (
	`id` integer PRIMARY KEY NOT NULL,
	`project_id` integer NOT NULL,
	`name` text NOT NULL
);
--> statement-breakpoint
CREATE UNIQUE INDEX IF NOT EXISTS `projects_name_unique` ON `projects` (`name`);
--> statement-breakpoint
CREATE UNIQUE INDEX IF NOT EXISTS `versions_project_id_name_unique` ON `versions` (`project_id`, `name`);
	)";
};

void migrate(SQLite::Database &database) {
	SQLite::Transaction transaction(database);

	database.exec(migrations());

	transaction.commit();
}

class DB {
private:
	SQLite::Database _database;
public:
	DB(const std::string path) : _database(path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE) {}

	SQLite::Database &get() {
		return this->_database;
	}

	std::vector<std::map<std::string, std::string>> query(SQLite::Statement &statement) {
		std::vector<std::map<std::string, std::string>> result;

		while (statement.executeStep()) {
			std::map<std::string, std::string> row;
			for (int i = 0; i < statement.getColumnCount(); i++) {
				row[statement.getColumnName(i)] = statement.getColumn(i).getText();
			}
			result.push_back(row);
		}

		return result;
	}
};