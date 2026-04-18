default:
    @just --list

test: functest unittest lint

unittest:
    make unittest

lint:
    cppcheck --library=posix --suppress=missingIncludeSystem --suppress=checkersReport --enable=all --error-exitcode=1 herescript.c test-herescript.c

rebuild: clean build

build:
    make _build/herescript _build/test-herescript

functest: build
    just -f functests/Justfile functest

clean:
    make clean
    rm -f _build/*.sh
    just -f functests/Justfile clean

install:
    make install

# Initialize decision records
init-decisions:
    python3 scripts/decisions.py --init

# Add a new decision record
add-decision TOPIC:
    python3 scripts/decisions.py --add "{{TOPIC}}"

# Quick start for Ubuntu based systems
quick-start-ubuntu:
    sudo apt update
    command -v python3 || sudo apt install -y python3
    command -v cppcheck || sudo apt install -y cppcheck
    command -v make || sudo apt install -y make
    command -v gcc || sudo apt install -y gcc
    command -v clang || sudo apt install -y clang