#include <string>
#include <vector>
#include <SQLiteCpp/SQLiteCpp.h>

#ifndef DATABASE_H
#define DATABASE_H

std::string migrations();

void migrate(SQLite::Database &database);

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

#endif // DATABASE_H