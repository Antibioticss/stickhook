# stickhook

[English](README.md) | [中文](README.zh-CN.md)

_This project is still under development_

A simple and easy-to-use **static** inline hook framework for **jailed** iOS devices and macOS

## Usage

1. Call `stick_init()` once in your hook library
2. Use `stick_hook` or `stick_replace` to declare hooks
3. After compiling, use `stickprep` to install static patches into the target binary

## Documentation

### stickhook library

```c
int stick_init(void);
```

Returns `0` on success.

```c
void stick_hook(char *image_name, uint64_t vmaddr, void *replacement, void **originptr);
```

> `stick_hook` is implemented as a C macro, this declaration is for reference only

Declare a function hook, and save original function

- `image_name` — file name of the target binary
- `vmaddr` — virtual memory address of the function to hook within the image
- `replacement` — the replacement function
- `originptr` — pointer to store the original function

**Note:** hooks for the same image must be **declared consecutively**

```c
void stick_replace(char *image_name, uint64_t vmaddr, void *replacement);
```

Same as above, but does not save original function

### stickprep tool

```bash
stickprep <library> <target>
```

> the filename of `target` must match `image_name` specified in the `library`

Patch static hooks into `target` and update runtime metadata in `library`.

If `library` has hooks for multiple binaries, `stickprep` must be run once for each target binary.

**Note:** `stickprep` modifies **both** `library` and `target`. **Do not** run it more than once on the same binary.

## Example

See the `test` directory for a working example.
