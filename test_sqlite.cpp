#include <SQLiteCpp/SQLiteCpp.h>
#include <iostream>

int main() {
    try {
        SQLite::Database db(":memory:", SQLite::OPEN_READWRITE|SQLite::OPEN_CREATE);
        std::cout << "SQLiteCpp is working!" << std::endl;
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    return 0;
}
