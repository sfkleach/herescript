# Implement scripts/test.py

Write a python script in `scripts/functest.py` that runs a series of tests. 

- This is run with the top-level directory as the working directory.

- The tests are drawn from all files matching `functests/*.yaml` 

- At the start of the test run, all files matching `_build/*.sh` are deleted.

- Each matching file has a series of tests with a name, a script to test,
  and expected output.

- Each test is executed as follows.
    1. A test script is generated in the build folder (_build/) by copying
       the script into a file `_build/{{NAME}}.sh`
    2. The first line of the script must be `#!${BUILD_DIR}/runscript ${BUILD_DIR}/test-runscript`
       or execution is blocked. This is a safety feature.
    3. Any occurrences of `${BUILD_DIR}` in that script are replaced by 
       the absolute path of the build folder.
    4. The file is made executable and then executed as a shebang script
       and the actual output is captured.
    5. The expected output has all occurences of ${BUILD_DIR} expanded into
       the absolute path of the build folder.
    6. The expected and actual outputs are compared and should be equal for
       the test to pass.

- A summary of the passes and fails is printed and a list of failing tests
  is printed.
