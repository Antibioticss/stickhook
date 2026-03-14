# stickhook

_This repo is still under development_

A simple static inline hook framework for jailed iOS devices (and macOS)

You don't have to run the dylib once to make it work!

## Usage

Call `stick_init()` once before using `stick_hook` or `stick_replace`

After compiling, use `stickprep` to install static hooks to the target binary and update info in the dylib

```bash
stickprep <library> <target>
```

## API

```c
int stick_init(void);
```

Return `0` on success

```c
void stick_hook(char *image_name, uint64_t vmaddr, void *replacement, void **originptr);
```

- `image_name` image file name
- `vmaddr` vmaddr of the function in the image
- `replacement` new implementation function pointer
- `originptr` pointer to a pointer which will store original function

Note that this function is implemented using C macros, use it carefully

```c
void stick_replace(char *image_name, uint64_t vmaddr, void *replacement);
```

Similar to above, but without `originptr` argument

## Example

Check out the `test` directory
