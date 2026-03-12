# stickhook

_This repo is still under development_

A simple static inline hook framework for jailed iOS devices (and macOS)

You don't have to run the dylib once to make it work!

## Usage

Call `stick_init()` once before calling `stick_hook` or `stick_replace`

After compiling, use `stickprep` to install static hooks to the target binary and update info in the dylib

```bash
stickprep <dylib> <binary>
```

## Example

Check out the `test` directory
