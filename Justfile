default:
    @just --list

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