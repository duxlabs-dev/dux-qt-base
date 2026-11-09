builddir := 'build'
lintdir := env_var_or_default('XDG_CACHE_HOME', env_var('HOME') / '.cache') / 'dux-qt-base'
# keep in sync with shell.nix
tidyfox_rev := '9d85d7e7dea2602aa74ec3168955fee69967e92f'

fmt:
    find src -type f \( -name "*.cpp" -o -name "*.hpp" \) -print0 | xargs -0 clang-format -i

lint:
	find src -type f -name "*.cpp" -print0 | parallel -j$(nproc) -q0 --no-notice --will-cite --tty --bar clang-tidy --load={{ env_var("TIDYFOX") }}

lint-ci:
	find src -type f -name "*.cpp" -print0 | parallel -j$(nproc) -q0 --no-notice --will-cite --tty clang-tidy --load={{ env_var("TIDYFOX") }}

lint-changed:
	git diff --name-only HEAD | grep "^.*\.cpp\$" |  parallel -j$(nproc) --no-notice --will-cite --tty --bar clang-tidy --load={{ env_var("TIDYFOX") }}

lint-staged:
	git diff --staged --name-only --diff-filter=d HEAD | grep "^.*\.cpp\$" |  parallel -j$(nproc) --no-notice --will-cite --tty --bar clang-tidy --load={{ env_var("TIDYFOX") }}

# Outside the nix shell: builds tidyfox for the system clang-tidy and a clang tree to lint against.
lint-setup:
	test -d {{lintdir}}/tidyfox || git clone https://git.outfoxxed.me/outfoxxed/tidyfox.git {{lintdir}}/tidyfox
	git -C {{lintdir}}/tidyfox checkout -q {{tidyfox_rev}}
	cmake -GNinja -S {{lintdir}}/tidyfox -B {{lintdir}}/tidyfox/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
	cmake --build {{lintdir}}/tidyfox/build
	cmake -GNinja -S . -B {{lintdir}}/lint -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
		-DNO_PCH=ON -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	cmake --build {{lintdir}}/lint

# Lints the given files, or those changed since HEAD, against the lint-setup tree.
lint-host *FILES:
	#!/bin/sh
	files="{{FILES}}"
	[ -n "$files" ] || files=$(git diff --name-only HEAD -- '*.cpp'; git ls-files --others --exclude-standard -- '*.cpp')
	printf '%s\n' $files | xargs -r -P "$(nproc)" -n 1 clang-tidy --quiet \
		--load={{lintdir}}/tidyfox/build/libtidyfox.so -p {{lintdir}}/lint

test-host *ARGS:
	cmake --build {{lintdir}}/lint
	ctest --test-dir {{lintdir}}/lint --output-on-failure {{ARGS}}

configure target='debug' *FLAGS='':
	cmake -GNinja -B {{builddir}} \
		-DCMAKE_BUILD_TYPE={{ if target == "debug" { "Debug" } else { "RelWithDebInfo" } }} \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON {{FLAGS}}

	ln -sf {{builddir}}/compile_commands.json compile_commands.json

_configure_if_clean:
	@if ! [ -d {{builddir}} ]; then just configure; fi

build: _configure_if_clean
	cmake --build {{builddir}}

release: (configure "release") build

clean:
	rm -f compile_commands.json
	rm -rf {{builddir}}

run *ARGS='': build
	{{builddir}}/src/dux-qt-base {{ARGS}}

test *ARGS='': build
	ctest --test-dir {{builddir}} --output-on-failure {{ARGS}}

install *ARGS='':
	cmake --install {{builddir}} {{ARGS}}
