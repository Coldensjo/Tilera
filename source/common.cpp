//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "common.h"
#include "math.h"

#include <sstream>
#include <random>
#include <regex>
#include <algorithm>
#include <cctype>

// random generator
std::mt19937& getRandomGenerator() {
	static std::random_device rd;
	static std::mt19937 generator(rd());
	return generator;
}

int32_t uniform_random(int32_t minNumber, int32_t maxNumber) {
	static std::uniform_int_distribution<int32_t> uniformRand;
	if (minNumber == maxNumber) {
		return minNumber;
	} else if (minNumber > maxNumber) {
		std::swap(minNumber, maxNumber);
	}
	return uniformRand(getRandomGenerator(), std::uniform_int_distribution<int32_t>::param_type(minNumber, maxNumber));
}

int32_t uniform_random(int32_t maxNumber) {
	return uniform_random(0, maxNumber);
}

//
std::string i2s(const int _i) {
	static std::stringstream ss;
	ss.str("");
	ss << _i;
	return ss.str();
}

std::string f2s(const double _d) {
	static std::stringstream ss;
	ss.str("");
	ss << _d;
	return ss.str();
}

wxString i2ws(const int _i) {
	wxString str;
	str << _i;
	return str;
}

void to_lower_str(std::string& source) {
	std::transform(source.begin(), source.end(), source.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
}

void to_upper_str(std::string& source) {
	std::transform(source.begin(), source.end(), source.begin(), [](unsigned char c) {
		return static_cast<char>(std::toupper(c));
	});
}

std::string as_lower_str(std::string other) {
	to_lower_str(other);
	return other;
}

std::string as_upper_str(std::string other) {
	to_upper_str(other);
	return other;
}

bool isFalseString(const std::string& str) {
	if (str == "false" || str == "0" || str == "" || str == "no" || str == "not") {
		return true;
	}
	return false;
}

bool isTrueString(const std::string& str) {
	return !isFalseString(str);
}

int random(int low, int high) {
	if (low == high) {
		return low;
	}

	if (low > high) {
		return low;
	}

	int range = high - low;

	double dist = double(mt_randi()) / 0xFFFFFFFF;
	return low + min(range, int((1 + range) * dist));
}

int random(int high) {
	return random(0, high);
}

std::wstring string2wstring(const std::string& utf8string) {
	wxString s(utf8string.c_str(), wxConvUTF8);
	return std::wstring((const wchar_t*)s.c_str());
}

std::string wstring2string(const std::wstring& widestring) {
	wxString s(widestring.c_str());
	return std::string((const char*)s.mb_str(wxConvUTF8));
}

namespace {
std::string trimPositionInput(std::string input) {
	auto notSpace = [](unsigned char c) {
		return !std::isspace(c);
	};
	input.erase(input.begin(), std::find_if(input.begin(), input.end(), notSpace));
	input.erase(std::find_if(input.rbegin(), input.rend(), notSpace).base(), input.end());
	return input;
}

bool parseTripleMatch(const std::smatch& match, int& x, int& y, int& z) {
	if (match.size() < 4) {
		return false;
	}

	try {
		x = std::stoi(match[1].str());
		y = std::stoi(match[2].str());
		z = std::stoi(match[3].str());
		return true;
	} catch (const std::exception&) {
		return false;
	}
}

bool extractLabeled(const std::string& input, std::initializer_list<const char*> names, int& value) {
	for (const char* name : names) {
		const std::string pattern = std::string(R"re(\b)re") + name + R"re(\s*[=:]\s*\"?(\d+)\"?)re";
		std::smatch match;
		if (std::regex_search(input, match, std::regex(pattern, std::regex::icase)) && match.size() >= 2) {
			try {
				value = std::stoi(match[1].str());
				return true;
			} catch (const std::exception&) {
			}
		}
	}
	return false;
}

bool extractJsonField(const std::string& input, const char* field, int& value) {
	const std::string pattern = std::string(R"re(")re") + field + R"re("\s*:\s*(\d+))re";
	std::smatch match;
	if (std::regex_search(input, match, std::regex(pattern, std::regex::icase)) && match.size() >= 2) {
		try {
			value = std::stoi(match[1].str());
			return true;
		} catch (const std::exception&) {
		}
	}
	return false;
}

bool assignPosition(Position& position, int x, int y, int z) {
	position = Position(x, y, z);
	return position.isValid();
}
} // namespace

bool posFromString(const std::string& rawInput, Position& position) {
	const std::string input = trimPositionInput(rawInput);
	if (input.empty()) {
		return false;
	}

	int x = -1;
	int y = -1;
	int z = -1;
	std::smatch match;

	// Selection range: {fromx = ..., tox = ..., fromy = ..., toy = ..., z = ...}
	if (extractLabeled(input, {"fromx"}, x) && extractLabeled(input, {"fromy"}, y)) {
		if (!extractLabeled(input, {"z", "fromz"}, z)) {
			z = 0;
		}
		return assignPosition(position, x, y, z);
	}

	// Position(x, y, z)
	static const std::regex positionCtor(R"(Position\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\))", std::regex::icase);
	if (std::regex_search(input, match, positionCtor) && parseTripleMatch(match, x, y, z)) {
		return assignPosition(position, x, y, z);
	}

	// Labeled coordinates: {x = 0, y = 0, z = 0}, x="0" y="0" z="0", centerx="0" ...
	if (extractLabeled(input, {"x", "centerx"}, x) && extractLabeled(input, {"y", "centery"}, y) && extractLabeled(input, {"z", "centerz"}, z)) {
		return assignPosition(position, x, y, z);
	}

	// JSON: {"x":0,"y":0,"z":0}
	if (extractJsonField(input, "x", x) && extractJsonField(input, "y", y) && extractJsonField(input, "z", z)) {
		return assignPosition(position, x, y, z);
	}

	// Parenthesized: (x, y, z)
	static const std::regex parenTriple(R"(\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\))");
	if (std::regex_search(input, match, parenTriple) && parseTripleMatch(match, x, y, z)) {
		return assignPosition(position, x, y, z);
	}

	// Colon-separated: x:y:z
	static const std::regex colonTriple(R"((?:^|[^\d])(\d+)\s*:\s*(\d+)\s*:\s*(\d+)(?:\D|$))");
	if (std::regex_search(input, match, colonTriple) && parseTripleMatch(match, x, y, z)) {
		return assignPosition(position, x, y, z);
	}

	// Comma-separated: x, y, z
	static const std::regex commaTriple(R"((?:^|[^\d])(\d+)\s*,\s*(\d+)\s*,\s*(\d+))");
	if (std::regex_search(input, match, commaTriple) && parseTripleMatch(match, x, y, z)) {
		return assignPosition(position, x, y, z);
	}

	// Whitespace-separated: x y z
	static const std::regex whitespaceTriple(R"((?:^|[^\d])(\d+)\s+(\d+)\s+(\d+)(?:\D|$))");
	if (std::regex_search(input, match, whitespaceTriple) && parseTripleMatch(match, x, y, z)) {
		return assignPosition(position, x, y, z);
	}

	// Fallback: exactly three numbers in the string.
	std::vector<int> numbers;
	static const std::regex digitToken(R"(\d+)");
	for (std::sregex_iterator it(input.begin(), input.end(), digitToken), end; it != end; ++it) {
		try {
			numbers.push_back(std::stoi(it->str()));
		} catch (const std::exception&) {
		}
	}
	if (numbers.size() == 3) {
		return assignPosition(position, numbers[0], numbers[1], numbers[2]);
	}

	return false;
}

bool posFromClipboard(Position& position, const int mapWidth /* = MAP_MAX_WIDTH */, const int mapHeight /* = MAP_MAX_HEIGHT */) {
	if (!wxTheClipboard->Open()) {
		return false;
	}

	if (!wxTheClipboard->IsSupported(wxDF_TEXT)) {
		wxTheClipboard->Close();
		return false;
	}

	wxTextDataObject data;
	wxTheClipboard->GetData(data);
	wxTheClipboard->Close();

	const std::string input = data.GetText().ToStdString();
	if (!posFromString(input, position)) {
		return false;
	}

	if (position.x > mapWidth || position.y > mapHeight) {
		return false;
	}

	return true;
}

wxString b2yn(bool value) {
	return value ? "Yes" : "No";
}

wxColor colorFromEightBit(int color) {
	if (color <= 0 || color >= 216) {
		return wxColor(0, 0, 0);
	}
	const uint8_t red = (uint8_t)(int(color / 36) % 6 * 51);
	const uint8_t green = (uint8_t)(int(color / 6) % 6 * 51);
	const uint8_t blue = (uint8_t)(color % 6 * 51);
	return wxColor(red, green, blue);
}
