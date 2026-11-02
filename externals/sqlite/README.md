# SQLite

SQLite 3.44.2 amalgamation (`sqlite3.c`, `sqlite3.h`), unmodified.

- Source: the official `sqlite-autoconf-3440200.tar.gz`, as bundled in the npm package `sqlite3@5.1.7`
  (`deps/sqlite-autoconf-3440200.tar.gz`; npm integrity
  `sha512-GGIyOiFaG+TUra3JIfkI/zGP8yZYLPQ0pl1bH+ODjiX57sPhrLU5sQJn1y9bDKZUFYkX1crlrPfSYt0BKKdkog==`).
- License: public domain (see the header of `sqlite3.c`).
- Built as the static library `ctr_sqlite` (see `CMakeLists.txt`) for the Special Nights stats database.

To upgrade, replace both files with a newer amalgamation from https://sqlite.org/download.html and update this note.
