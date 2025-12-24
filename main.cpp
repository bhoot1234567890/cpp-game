#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#endif

// RAII class for terminal mode management (prevents terminal state bugs)
#ifndef _WIN32
class TerminalMode {
    struct termios original;
    bool valid = false;
public:
    TerminalMode() {
        if (tcgetattr(STDIN_FILENO, &original) == 0) {
            valid = true;
        }
    }
    ~TerminalMode() {
        if (valid) {
            tcsetattr(STDIN_FILENO, TCSADRAIN, &original);
        }
    }
    void setRaw() {
        if (!valid) return;
        struct termios raw = original;  // Copy original, don't modify it!
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    void restore() {
        if (valid) {
            tcsetattr(STDIN_FILENO, TCSADRAIN, &original);
        }
    }
};
#endif

// Cross-platform getch with proper terminal state handling (uses RAII TerminalMode)
char getch() {
#ifdef _WIN32
    return _getch();
#else
    char buf = 0;
    TerminalMode tm; // RAII: will restore terminal state when out of scope
    tm.setRaw();
    ssize_t n = read(STDIN_FILENO, &buf, 1);
    return (n == 1) ? buf : 0;
#endif
}

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <cctype>
#include <yaml-cpp/yaml.h>



// ANSI color codes (constexpr instead of macros)
static constexpr const char* COLOR_RESET   = "\033[0m";
static constexpr const char* COLOR_PLAYER  = "\033[32m"; // Green
static constexpr const char* COLOR_NPC     = "\033[33m"; // Yellow
static constexpr const char* COLOR_WALL    = "\033[37m"; // White
static constexpr const char* COLOR_DIALOG  = "\033[36m"; // Cyan
static constexpr const char* COLOR_CHOICE  = "\033[35m"; // Magenta

static constexpr const char* COLOR_DOOR    = "\033[34m"; // Blue
static constexpr const char* COLOR_ITEM    = "\033[93m"; // Bright Yellow
static constexpr const char* COLOR_FLOOR   = "\033[90m"; // Dark Gray
static constexpr const char* COLOR_HEALTH  = "\033[91m"; // Bright Red
static constexpr const char* COLOR_BOLD    = "\033[1m";

struct GameState {
    int currentMap = 1;
    int coins = 0;
    int health = 100;
    int steps = 0;
    bool hasKey = false;
    std::string status;
    
    // Track what's underneath the player to restore when moving
    char underPlayer = ' ';
    
    // Track visited positions for potential scoring
    int tilesExplored = 0;
};

namespace {
    inline std::string keyForChar(char ch) {
        return std::string(1, ch);
    }

    inline bool isNpcChar(const YAML::Node& dialogueTree, char ch) {
        if (!std::isprint(static_cast<unsigned char>(ch))) return false;
        return dialogueTree[keyForChar(ch)].IsDefined();
    }

    // Offsets: left, right, up, down
    constexpr int OFFX[4] = {-1, 1, 0, 0};
    constexpr int OFFY[4] = {0, 0, -1, 1};

    bool isGameEntity(const std::vector<std::string>& map, int x, int y, char ch, const YAML::Node& dialogueTree) {
        if (ch == 'D' || isNpcChar(dialogueTree, ch)) {
            for (int d = 0; d < 4; ++d) {
                int nx = x + OFFX[d];
                int ny = y + OFFY[d];
                if (ny >= 0 && ny < static_cast<int>(map.size()) &&
                    nx >= 0 && nx < static_cast<int>(map[ny].size())) {
                    char neighbor = map[ny][nx];
                    if (std::isalpha(static_cast<unsigned char>(neighbor)) &&
                        neighbor != 'P' && neighbor != 'D' &&
                        !isNpcChar(dialogueTree, neighbor)) {
                        return false;
                    }
                }
            }
            return true;
        }
        return false;
    }

    void applyDialogueEffects(const YAML::Node& node, GameState& state) {
        if (!node["effects"] || !node["effects"].IsMap()) return;
        const auto& effects = node["effects"];

        if (effects["coins"]) {
            int delta = effects["coins"].as<int>();
            state.coins += delta;
        }
        if (effects["health"]) {
            int delta = effects["health"].as<int>();
            state.health = std::max(0, std::min(100, state.health + delta));
        }
        if (effects["key"]) {
            state.hasKey = effects["key"].as<bool>();
        }
        if (effects["message"]) {
            state.status = effects["message"].as<std::string>();
        }
    }
}

bool loadMap(const std::string& filename, std::vector<std::string>& map, int& playerX, int& playerY) {
    map.clear();
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open map file: " << filename << std::endl;
        return false;
    }
    std::string line;
    playerX = playerY = -1;
    int y = 0;
    while (std::getline(file, line)) {
        map.push_back(line);
        for (size_t x = 0; x < line.size(); ++x) {
            if (line[x] == 'P') {
                playerX = x;
                playerY = y;
            }
        }
        ++y;
    }
    
    // Validate player was found
    if (playerX < 0 || playerY < 0) {
        std::cerr << "Warning: No player spawn 'P' found in " << filename << std::endl;
        // Default to first open space
        for (int row = 0; row < static_cast<int>(map.size()) && playerY < 0; ++row) {
            for (int col = 0; col < static_cast<int>(map[row].size()); ++col) {
                if (map[row][col] == '.' || map[row][col] == ' ') {
                    playerX = col;
                    playerY = row;
                    map[row][col] = 'P';
                    break;
                }
            }
        }
    }
    return true;
}

void showDialogueTree(const YAML::Node& charNode, GameState& state) {
    std::string current = "start";
    while (true) {
        const auto& node = charNode[current];
        if (!node || !node["text"]) {
            state.status = "(Dialogue data missing for this node.)";
            break;
        }
        std::cout << "\n" << COLOR_DIALOG << node["text"].as<std::string>() << COLOR_RESET << "\n";
        applyDialogueEffects(node, state);
        if (!node["choices"]) break;
        std::vector<std::string> options;
        std::vector<std::string> nexts;
        for (const auto& choice : node["choices"]) {
            options.push_back(choice.second["text"].as<std::string>());
            nexts.push_back(choice.second["next"].as<std::string>());
        }
        for (size_t i = 0; i < options.size(); ++i) {
            std::cout << COLOR_CHOICE << (i+1) << ". " << options[i] << COLOR_RESET << std::endl;
        }
        std::cout << "Choose (or Q to exit): ";
        int pick = 0;
        while (true) {
            char c = getch();
            if (c == 'q' || c == 'Q') {
                std::cout << "Q" << std::endl;
                return;
            }
            if (c >= '1' && c <= '0' + static_cast<int>(options.size())) {
                pick = c - '1';
                std::cout << (pick+1) << std::endl;
                break;
            }
        }
        if (nexts[pick] == "end") break;
        current = nexts[pick];
    }
    std::cout << "\nPress any key to continue...";
    getch();
}

int main() {
    std::vector<std::string> map;
    int playerX = -1, playerY = -1;
    GameState state;
    std::string mapFiles[] = {"map1.txt", "map2.txt", "map3.txt", "map4.txt"};
    
    if (!loadMap(mapFiles[state.currentMap-1], map, playerX, playerY)) {
        std::cerr << "Failed to load initial map. Exiting." << std::endl;
        return 1;
    }
    
    // Initialize what's under the player (should be space or floor)
    state.underPlayer = ' ';

    // Load all dialogues from YAML file
    YAML::Node dialogueTree;
    try {
        dialogueTree = YAML::LoadFile("dialogue.yaml");
    } catch (const YAML::Exception& e) {
        std::cerr << "Failed to load dialogue.yaml: " << e.what() << std::endl;
        return 1;
    }
    char input;
    while (true) {
#ifdef _WIN32
        system("cls");
#else
        std::cout << "\033[2J\033[1;1H";
#endif
        // Enhanced HUD with health bar
        std::cout << COLOR_BOLD << "═══════════════════════════════════════" << COLOR_RESET << "\n";
        std::cout << " Map " << state.currentMap << "/4  │  ";
        std::cout << COLOR_ITEM << "Coins: " << state.coins << COLOR_RESET << "  │  ";
        std::cout << "Key: ";
        if (state.hasKey)
            std::cout << COLOR_PLAYER << "✓" << COLOR_RESET << "  │  ";
        else
            std::cout << COLOR_HEALTH << "✗" << COLOR_RESET << "  │  ";
        
        // Health bar visualization
        std::cout << COLOR_HEALTH << "HP: ";
        int healthBars = state.health / 10;
        std::cout << "[";
        for (int i = 0; i < 10; ++i) {
            std::cout << (i < healthBars ? "█" : "░");
        }
        std::cout << "]" << COLOR_RESET << "\n";
        std::cout << COLOR_BOLD << "═══════════════════════════════════════" << COLOR_RESET << "\n";
        
        if (!state.status.empty()) {
            std::cout << COLOR_DIALOG << "► " << state.status << COLOR_RESET << "\n";
        }
        std::cout << "\n";

        for (int y = 0; y < static_cast<int>(map.size()); ++y) {
            std::cout << " ";  // Left margin for better appearance
            for (int x = 0; x < static_cast<int>(map[y].size()); ++x) {
                char ch = map[y][x];
                if (ch == 'P')
                    std::cout << COLOR_PLAYER << "@" << COLOR_RESET;  // @ is classic roguelike player
                else if (ch == '#')
                    std::cout << COLOR_WALL << "█" << COLOR_RESET;  // Solid block for walls
                else if (ch == 'D' && isGameEntity(map, x, y, ch, dialogueTree))
                    std::cout << COLOR_DOOR << "◊" << COLOR_RESET;  // Diamond for door
                else if (ch == 'o')
                    std::cout << COLOR_ITEM << "●" << COLOR_RESET;  // Filled circle for coins
                else if (isNpcChar(dialogueTree, ch) && isGameEntity(map, x, y, ch, dialogueTree))
                    std::cout << COLOR_NPC << ch << COLOR_RESET;
                else if (ch == '.')
                    std::cout << COLOR_FLOOR << "·" << COLOR_RESET;  // Subtle floor dots
                else
                    std::cout << ch;
            }
            std::cout << std::endl;
        }

        // Nearby prompt
        bool canTalk = false;
        bool nearDoor = false;
        const int* dx = OFFX;
        const int* dy = OFFY;
        for (int d = 0; d < 4; ++d) {
            int cx = playerX + dx[d];
            int cy = playerY + dy[d];
            if (cy >= 0 && cy < static_cast<int>(map.size()) && cx >= 0 && cx < static_cast<int>(map[cy].size())) {
                char ch = map[cy][cx];
                if (isNpcChar(dialogueTree, ch)) canTalk = true;
                if (ch == 'D') nearDoor = true;
            }
        }
        std::cout << "\n";
        if (canTalk) std::cout << COLOR_NPC << " 💬 Press E to talk" << COLOR_RESET << "\n";
        if (nearDoor) std::cout << COLOR_DOOR << " 🚪 Walk into door to travel" << COLOR_RESET << "\n";
        std::cout << "\n" << COLOR_BOLD << "[W/A/S/D] Move  [E] Interact  [Q] Quit" << COLOR_RESET << ": ";

        input = getch();
        input = static_cast<char>(std::toupper(static_cast<unsigned char>(input)));
        if (input == 'Q') break;

        if (input == 'E') {
            // Check adjacent tiles for a character
            bool found = false;
            for (int d = 0; d < 4; ++d) {
                int cx = playerX + dx[d];
                int cy = playerY + dy[d];
                if (cy >= 0 && cy < static_cast<int>(map.size()) && cx >= 0 && cx < static_cast<int>(map[cy].size())) {
                    char ch = map[cy][cx];
                    if (isNpcChar(dialogueTree, ch)) {
                        showDialogueTree(dialogueTree[keyForChar(ch)], state);
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                std::cout << "\nNo one to interact with! Press any key...";
                getch();
            }
            continue;
        }

        int newX = playerX, newY = playerY;
        if (input == 'W') newY--;
        else if (input == 'S') newY++;
        else if (input == 'A') newX--;
        else if (input == 'D') newX++;
        else continue;

        // Bounds check FIRST (prevents out-of-bounds reads)
        if (newY < 0 || newY >= static_cast<int>(map.size()) || newX < 0 || newX >= static_cast<int>(map[newY].size())) {
            state.status = "You bump into the edge of the world.";
            continue;
        }

        // Check wall / NPC / items / door
        char dest = map[newY][newX];
        if (dest == 'D') {
            if (!state.hasKey && state.currentMap == 1) {
                state.status = "The door is locked. Someone nearby might know a trick...";
                continue;
            }
            // Go to next map (wrap around)
            state.currentMap = (state.currentMap % 4) + 1;
            if (!loadMap(mapFiles[state.currentMap-1], map, playerX, playerY)) {
                state.status = "Error loading next map!";
                state.currentMap = ((state.currentMap - 2 + 4) % 4) + 1;  // Revert
                continue;
            }
            state.underPlayer = ' ';  // Reset what's under player for new map
            state.status = "You step through the doorway.";
            continue;
        }
        if (dest == 'o') {
            state.coins += 1;
            state.status = "\u2728 Picked up a coin! (+1)";
            map[newY][newX] = '.'; // consume the coin
            dest = '.';
        }

        // Check if destination is walkable (only allow movement on specific tiles)
        bool isWalkable = (dest == '.' || dest == ' ');
        
        if (isWalkable && dest != '#' && !isNpcChar(dialogueTree, dest)) {
            map[playerY][playerX] = state.underPlayer;  // Restore what was under player
            state.underPlayer = dest;                    // Remember what's at new position
            map[newY][newX] = 'P';                       // Move player to new position
            playerY = newY;
            playerX = newX;
            state.steps++;
            state.status.clear();  // Clear status after moving
        } else if (!isWalkable && dest != '#' && dest != 'D' && !isNpcChar(dialogueTree, dest)) {
            state.status = "Can't walk through that.";
        }
    }
    
    // Game over summary
    std::cout << "\n" << COLOR_BOLD << "═══════════════════════════════════════" << COLOR_RESET << "\n";
    std::cout << COLOR_DIALOG << "Thanks for playing!" << COLOR_RESET << "\n";
    std::cout << "Steps taken: " << state.steps << "\n";
    std::cout << "Coins collected: " << state.coins << "\n";
    std::cout << COLOR_BOLD << "═══════════════════════════════════════" << COLOR_RESET << "\n";
    return 0;
}