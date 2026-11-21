#!/usr/bin/env python3
"""Installed NAME_PREFIX/SCHEMA_ID_VARIABLE and cmake -P policy regression."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('source', 'build-dir', 'cmake'):
        parser.add_argument('--' + option, required=True)
    args = parser.parse_args()
    source = Path(args.source).resolve()
    with tempfile.TemporaryDirectory(prefix='quarry-name-prefix-cmake-') as temporary:
        work = Path(temporary)
        prefix = work / 'install'

        def run(command, success=True):
            result = subprocess.run(list(map(str, command)), cwd=work, text=True,
                                    capture_output=True, check=False)
            assert (result.returncode == 0) == success, result.stdout + result.stderr
            return result.stdout + result.stderr

        run([args.cmake, '--install', args.build_dir, '--prefix', prefix])
        compiler = prefix / 'bin/quarry-schema-compiler'
        module = prefix / 'lib/cmake/Quarry/QuarryGenerate.cmake'
        roots = [source / 'examples/truck/schemas/configuration.brd',
                 source / 'examples/truck/multi_configuration/rev_b/configuration.brd']
        project = work / 'consumer'
        project.mkdir()
        cmakelists = '''cmake_minimum_required(VERSION 3.20)
project(prefix_helper LANGUAGES CXX)
find_package(Quarry CONFIG REQUIRED)
set(other rev_b)
if(COLLIDE)
  set(other rev_a)
endif()
set(compiler_option)
if(FAKE_COMPILER)
  list(APPEND compiler_option SCHEMA_COMPILER "${FAKE_COMPILER}")
endif()
quarry_generate_cpp(SCHEMA "@A@" OUTPUT_DIR "${CMAKE_BINARY_DIR}/gen" NAME_PREFIX rev_a
  SCHEMA_ID_VARIABLE id_a GENERATE truck.engine OUT_FILES files_a ${compiler_option})
quarry_generate_cpp(SCHEMA "@B@" OUTPUT_DIR "${CMAKE_BINARY_DIR}/gen" NAME_PREFIX ${other}
  SCHEMA_ID_VARIABLE id_b GENERATE truck.engine OUT_FILES files_b ${compiler_option})
quarry_generate_cpp(SCHEMA "@A@" OUTPUT_DIR "${CMAKE_BINARY_DIR}/gen" NAME_PREFIX rev_c
  SCHEMA_ID_VARIABLE id_c OUT_FILES files_c ${compiler_option})
if(NOT id_a STREQUAL "9200ca494f010801b830d0857cf66dde" OR NOT id_c STREQUAL id_a OR id_b STREQUAL id_a)
  message(FATAL_ERROR "configuration identity depends on prefix/selection or was not exported")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/results.txt" "${id_a}\\n${id_b}\\n${id_c}\\n${files_a}\\n")
add_custom_target(generate ALL DEPENDS ${files_a} ${files_b} ${files_c})
'''.replace('@A@', roots[0].as_posix()).replace('@B@', roots[1].as_posix())
        (project / 'CMakeLists.txt').write_text(cmakelists)
        build = work / 'build'
        configure = [args.cmake, '-Werror=dev', '-S', project, '-B', build,
                     '-DCMAKE_PREFIX_PATH=' + str(prefix)]
        run(configure)
        run([args.cmake, '--build', build, '--parallel', '4'])
        lines = (build / 'results.txt').read_text().splitlines()
        assert lines[:3] == ['9200ca494f010801b830d0857cf66dde',
                             '806e9a4e11d0df71c4e5b51045f85010',
                             '9200ca494f010801b830d0857cf66dde'], lines
        assert '/rev_a/truck/engine.generated.hpp' in lines[3], lines
        assert (build / 'gen/rev_b/truck/engine.generated.hpp').is_file()

        # Exactly the build-time inventory path, under cmake -P with no
        # cmake_minimum_required in the caller. #87's policy capture must hold.
        script = work / 'verify.cmake'
        script.write_text('set(QUARRY_VERIFY_OUTPUT_INVENTORY TRUE)\n'
                          f'set(QUARRY_VERIFY_COMPILER "{compiler.as_posix()}")\n'
                          f'set(QUARRY_VERIFY_SCHEMA "{roots[0].as_posix()}")\n'
                          f'set(QUARRY_VERIFY_OUTPUT_DIR "{(build / "gen").as_posix()}")\n'
                          'set(QUARRY_VERIFY_GENERATE "truck.engine")\n'
                          'set(QUARRY_VERIFY_NAME_PREFIX "rev_a")\n'
                          f'set(QUARRY_VERIFY_EXPECTED_OUTPUTS "{lines[3]}")\n'
                          f'include("{module.as_posix()}")\n'
                          f'_quarry_generate_query_schema_id("{compiler.as_posix()}" "{roots[0].as_posix()}" id)\n'
                          'if(NOT id STREQUAL "9200ca494f010801b830d0857cf66dde")\n'
                          '  message(FATAL_ERROR "script schema identity mismatch")\nendif()\n')
        run([args.cmake, '-Werror=dev', '-P', script])
        # The same installed registry must reject real output collisions.
        collision = run([*configure, '-B', work / 'collision', '-DCOLLIDE=ON'], success=False)
        assert 'output' in collision.lower() and ('claim' in collision.lower() or 'multiple' in collision.lower()), collision
        (project / 'CMakeLists.txt').write_text(cmakelists.replace('NAME_PREFIX rev_a', 'NAME_PREFIX rev_a NAME_PREFIX rev_b', 1))
        duplicate = run([*configure, '-B', work / 'duplicate'], success=False)
        assert 'duplicate NAME_PREFIX' in duplicate, duplicate
        (project / 'CMakeLists.txt').write_text(cmakelists)
        fake = work / 'bad-identity-compiler'
        fake.write_text('#!/usr/bin/env python3\nimport os, sys\n'
                        'if "--print-schema-id" in sys.argv:\n    print("not-a-schema-id")\n    sys.exit(0)\n'
                        f'os.execv({str(compiler)!r}, [{str(compiler)!r}, *sys.argv[1:]])\n')
        fake.chmod(0o755)
        malformed = run([*configure, '-B', work / 'malformed', '-DFAKE_COMPILER=' + str(fake)], success=False)
        assert 'malformed schema_id' in malformed, malformed
        print('Installed helper: prefix paths, complete identities, multiple calls, collisions, malformed query and cmake -P passed.')


if __name__ == '__main__':
    main()
