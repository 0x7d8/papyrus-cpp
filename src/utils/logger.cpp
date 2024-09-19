#include <iostream>
#include <string>

enum class Color {
	WHITE,
	RED,
	GREEN,
	BLUE
};

class Logger {
private:
	Color _color = Color::WHITE;

	std::string format(const Color &color, const std::string &message) {
		std::string colorCode;
		switch (color) {
			case Color::WHITE:
				colorCode = "\033[0m";
				break;
			case Color::RED:
				colorCode = "\033[31m";
				break;
			case Color::GREEN:
				colorCode = "\033[32m";
				break;
			case Color::BLUE:
				colorCode = "\033[34m";
				break;
		}
		return colorCode + message + "\033[0m";
	}
public:
	static Logger color(Color color) {
		Logger logger;
		logger._color = color;

		return logger;
	}

	Logger log(const std::string &message) {
		std::cout << format(_color, message) << std::endl;

		return *this;
	}
};