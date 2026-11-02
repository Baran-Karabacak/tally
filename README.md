# tally

A small command-line budget tracker written in C, backed by PostgreSQL.

## Build

The project is built with [nob.h](https://github.com/tsoding/nob.h) by
Tsoding, a header-only "no build" library: the build script is plain C
(`nob.c`) and rebuilds itself when it changes, so no Make or CMake is needed.
A copy of `nob.h` (v3.10.1) is vendored in this repository.

```sh
sudo apt install libpq-dev docker.io docker-compose-v2
docker compose up -d
cc -o nob nob.c      # bootstrap, only needed once
./nob test           # run the tests
./nob                # release build -> build/tally
./nob debug          # build with AddressSanitizer and UBSan
```

## Usage

```sh
tally add 45.50 food --note "lunch"
tally income 3000 scholarship
tally list --week
```

## Configuration

By default tally connects to the local development database from
`compose.yaml`. Set `TALLY_DB_URL` to use a different one. The credentials in
`compose.yaml` are for local development only.

## Credits

- [nob.h](https://github.com/tsoding/nob.h) by Alexey Kutepov (Tsoding):
  build system and utility macros.
- [PostgreSQL](https://www.postgresql.org/) and its C client library libpq.
- The date arithmetic in `src/date.c` follows Howard Hinnant's
  ["days from civil"](https://howardhinnant.github.io/date_algorithms.html)
  algorithms.