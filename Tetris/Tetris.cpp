#include <array>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include "Windows.h"

constexpr int screenWidth = 120;
constexpr int screenHeight = 30;
constexpr int screenPixels = screenWidth * screenHeight;
constexpr int fieldWidth = 12;
constexpr int fieldHeight = 18;
constexpr int fieldPixels = fieldWidth * fieldHeight;
constexpr int tetrominoWidth = 4;
constexpr int tetrominoHeight = 4;
constexpr int tetrominoPixels = tetrominoWidth * tetrominoHeight;

using namespace std::literals;

constexpr wchar_t emptyTetrominoPixel = L'.';
constexpr wchar_t concreteTetrominoPixel = L'X';
std::array<std::wstring, 7u> tetrominos = {
	L"..X."
	L"..X."
	L"..X."
	L"..X.",

	L"...."
	L".X.."
	L".XX."
	L".X..",

	L"...."
	L".XX."
	L".XX."
	L"....",

	L"...."
	L".X.."
	L".XX."
	L"..X.",

	L"...."
	L".X.."
	L".X.."
	L".XX.",

	L"...."
	L"..X."
	L".XX."
	L".X..",

	L"...."
	L"..X."
	L"..X."
	L".XX."
};
std::wstring tetrominoChars{ L"ABCDEFG" };

constexpr wchar_t borderCell{ L'#' };
constexpr wchar_t emptyCell{ L' ' };
constexpr wchar_t completedLineCell{ L'=' };
constexpr int fieldOffsetX = 2;
constexpr int fieldOffsetY = 6;
constexpr size_t RKey = 0;
constexpr size_t LKey = 1;
constexpr size_t DKey = 2;
constexpr size_t ZKey = 3;

enum class TetrominoType {
	Line = 0,
	Tee = 1,
	Cube = 2,
	LeftL = 3,
	RightL = 4,
	LeftS = 5,
	RightS = 6
};

struct ActiveTetromino {
	TetrominoType type = TetrominoType::Line;
	int posX = -1;
	int posY = -1;
	int rotation = 0;
	std::wstring shape;
};

auto getShape(const ActiveTetromino& tetromino) -> std::wstring {
	/*
	Rotation: +90deg
	x = y
	y = 3 - X

	Rotation : +180deg
	x = 3 - x
	y = 3 - y

	Rotation : +270deg
	x = 3 - y
	y = X
	*/

	auto rotation = tetromino.rotation;
	auto tetrominoDef = tetrominos[static_cast<size_t>(tetromino.type)];
	if (rotation == 0) {
		return tetrominoDef;
	}
	auto ret = std::wstring(tetrominoPixels, L'.');
	constexpr auto offsetX = tetrominoWidth - 1;
	constexpr auto offsetY = tetrominoHeight - 1;
	for (int x = 0; x < tetrominoWidth; ++x) {
		for (int y = 0; y < tetrominoHeight; ++y) {
			int targetX;
			int targetY;
			switch (tetromino.rotation) {
			case 1:
				targetX = y;
				targetY = offsetY - x;
				break;
			case 2:
				targetX = offsetX - x;
				targetY = offsetY - y;
				break;
			case 3:
				targetX = offsetX - y;
				targetY = x;
				break;
			default:
				targetX = x;
				targetY = y;
				break;
			}

			auto index = targetY * tetrominoWidth + targetX;
			ret[index] = tetrominoDef[y * tetrominoWidth + x];
		}
	}
	return ret;
}

auto rotate(ActiveTetromino& tetromino, bool clockwise = true) -> void {
	tetromino.rotation = (tetromino.rotation + (clockwise ? 1 : -1)) % 4;
	tetromino.shape = getShape(tetromino);
}

auto doesPieceFit(const ActiveTetromino& tetromino, const std::wstring& field) -> boolean {
	auto bottomRowIndex = tetromino.posY + tetrominoHeight - 1;
	for (int i = 0; i < tetrominoWidth; ++i) {
		for (int j = tetrominoHeight - 1; j >= 0; --j) {
			if (tetromino.shape.at(j * tetrominoWidth + i) == emptyTetrominoPixel) {
				continue;
			}
			auto fieldRow = tetromino.posY + j;
			// We're checking a piece of the tetromino that isn't yet in the field
			if (fieldRow < 0) {
				continue;
			}
			if (field.at(fieldRow * fieldWidth + (tetromino.posX + i)) == emptyCell) {
				continue;
			}
			return false;
		}
	}
	return true;
}

auto cementify(const ActiveTetromino& tetromino, std::wstring& field) -> void {
	for (int i = 0; i < tetrominoWidth; ++i) {
		for (int j = 0; j < tetrominoWidth; ++j) {
			auto fieldX = tetromino.posX + i;
			auto fieldY = tetromino.posY + j;
			auto tetrominoCell = tetromino.shape[j * tetrominoWidth + i];
			if (tetrominoCell == emptyTetrominoPixel) {
				continue;
			}
			field[fieldY * fieldWidth + fieldX] = L"ABCDEFG"[static_cast<int>(tetromino.type)];
		}
	}
}


int main()
{
	srand(static_cast<uint32_t>(GetTickCount64()));

	// Create Screen Buffer
	auto screen = std::wstring(screenPixels, L' ');
	auto field = std::wstring(fieldPixels, emptyCell);
	for (int i = 0; i < fieldPixels; ++i) {
		bool isLeftEdge = i % fieldWidth == 0;
		bool isRightEdge = i % fieldWidth == fieldWidth - 1;
		bool isBottom = i / fieldWidth == fieldHeight - 1;
		field[i] = isLeftEdge || isRightEdge || isBottom ? borderCell : emptyCell;
	}

	HANDLE hConsole = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, 0, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
	SetConsoleActiveScreenBuffer(hConsole);
	DWORD dwBytesWritten = 0;

	// Game logic
	std::optional<ActiveTetromino> currentTetromino;
	constexpr int tetrominoEntryPointX = 4;
	constexpr int tetrominoEntryPointY = -2;

	constexpr int nthPieceSpeedup = 10;
	int movementThreshold = 20;
	int speedCounter = 0;
	int placedPiecesCounter = 0;
	int score = 0;

	std::array<bool, ZKey + 1> pressedKeys; // R, L, D, Z
	bool isRotating = false;
	bool isTranslating = false;
	bool gameOver = false;

	while (true) // Main Loop
	{
		std::this_thread::sleep_for(50ms); // Small Step = 1 Game Tick

		if (!currentTetromino.has_value()) {
			auto newTetromino = ActiveTetromino{
				static_cast<TetrominoType>(rand() % 7),
				tetrominoEntryPointX,
				tetrominoEntryPointY
			};
			newTetromino.shape = getShape(newTetromino);
			currentTetromino = newTetromino;
		}
		auto tetromino = currentTetromino.value();
		if (!doesPieceFit(tetromino, field)) {
			gameOver = true;
			break;
		}

		// Input
		for (size_t k = RKey; k <= ZKey; ++k) {						     // R   L   D Z
			pressedKeys[k] = (0x8000 & GetAsyncKeyState((unsigned char)("\x27\x25\x28Z"[k]))) != 0;
		}

		if (pressedKeys[ZKey]) {
			if (!isRotating) {
				rotate(tetromino);
				if (!doesPieceFit(tetromino, field)) {
					rotate(tetromino, false);
					//tetromino.posX += tetromino.posX == 0 ? 1 : -1;
				}
				isRotating = true;
			}
			else {
				isRotating = false;
			}
		}

		bool pushDown = pressedKeys[DKey];
		bool goLeft = pressedKeys[LKey];
		bool goRight = pressedKeys[RKey];

		// Game logic - tetromino movement
		if (goLeft ^ goRight) {
			if (!isTranslating) {
				if (goLeft) {
					--tetromino.posX;
					if (!doesPieceFit(tetromino, field)) {
						++tetromino.posX;
					}
				}
				else {
					++tetromino.posX;
					if (!doesPieceFit(tetromino, field)) {
						--tetromino.posX;
					}
				}
				isTranslating = true;
			}
			else {
				isTranslating = false;
			}
		}

		bool hasCementified = false;
		auto completedLinesIndexes = std::vector<int>{};
		if (pushDown || ++speedCounter == movementThreshold) {
			speedCounter = 0;
			++tetromino.posY;
			if (!doesPieceFit(tetromino, field)) {
				--tetromino.posY;
				if (tetromino.posY < 0) {
					gameOver = true;
					break;
				}
				cementify(tetromino, field);
				currentTetromino.reset();
				hasCementified = true;
				if (++placedPiecesCounter % nthPieceSpeedup == 0 && movementThreshold > 2) {
					--movementThreshold;
				}

				// Exclude floor
				for (int y = 0; y < fieldHeight - 1; ++y) {
					bool isComplete = true;
					for (int x = 0; x < fieldWidth; ++x) {
						int fieldCell = y * fieldWidth + x;
						if (field[fieldCell] == emptyCell) {
							isComplete = false;
							break;
						}
					}
					if (isComplete) {
						completedLinesIndexes.push_back(y);
						// Exclude walls
						for (int x = 1; x < fieldWidth - 1; ++x) {
							int fieldCell = y * fieldWidth + x;
							field[fieldCell] = completedLineCell;
						}
					}
				}

				score += 25 + (static_cast<int>(!completedLinesIndexes.empty()) << completedLinesIndexes.size()) * 100;
			}
		}

		// Draw the field
		for (int x = 0; x < fieldWidth; ++x) {
			for (int y = 0; y < fieldHeight; ++y) {
				auto screenPixel = (y + fieldOffsetY) * screenWidth + (x + fieldOffsetX);
				screen[screenPixel] = field[y * fieldWidth + x];
			}
		}

		// Draw the score
		swprintf_s(&screen[2 * screenWidth + fieldWidth + 6], 16, L"SCORE: %8d", score);

		// There's not a piece to draw after cementifying
		if (!hasCementified) {
			// Draw the tetromino
			for (int x = 0; x < tetrominoWidth; ++x) {
				for (int y = 0; y < tetrominoHeight; ++y) {
					// Don't drow the tetromino if it's outside the playing field
					auto tetrominoPixelX = tetromino.posX + x;
					auto tetrominoPixelY = tetromino.posY + y;
					if (tetrominoPixelX < 0 || tetrominoPixelX >= fieldWidth ||
						tetrominoPixelY < 0 || tetrominoPixelY >= fieldHeight) {
						continue;
					}
					auto tetrominoPixel = tetromino.shape[y * tetrominoWidth + x];
					// Don't draw the dots
					if (tetrominoPixel == emptyTetrominoPixel) {
						continue;
					}
					auto screenPixel = (y + fieldOffsetY + tetromino.posY) * screenWidth + (x + fieldOffsetX + tetromino.posX);
					screen[screenPixel] = tetrominoPixel;
				}
			}
			currentTetromino = tetromino;
		}
		else if (completedLinesIndexes.size() > 0) {
			WriteConsoleOutputCharacter(hConsole, screen.c_str(), screenPixels, { 0, 0 }, &dwBytesWritten);
			std::this_thread::sleep_for(400ms);

			for (auto lineIndex : completedLinesIndexes) {
				for (int y = lineIndex; y > 0; --y) {
					for (int x = 1; x < fieldWidth - 1; ++x) {
						field[y * fieldWidth + x] = field[(y - 1) * fieldWidth + x];
					}
				}
			}
		}

		WriteConsoleOutputCharacter(hConsole, screen.c_str(), screenPixels, { 0, 0 }, &dwBytesWritten);
	}

	// Oh Dear
	CloseHandle(hConsole);
	std::cout << "Game Over!! Score:" << score << std::endl;
	system("pause");
	return 0;
}
