# A simple test for stickhook

This test is designed to run on macOS

Note that you might have to update the offset in `test.c`

```bash
clang test.c -shared -o libtest.dylib -lstickhook -L..
clang main.c -o main -ltest -L.
../stickprep libtest.dylib main
codesign -f -s - main
codesign -f -s - libtest.dylib
./main
```

If everything works you'll get

```plain
====== ask_serial hooked!
Enter your serial number: 1234
====== verifying serial: 1234
Invalid serial number!
====== result: 0
====== faking valid...
====== ask_serial returned!
Hello world!
```
