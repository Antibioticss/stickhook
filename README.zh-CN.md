# stickhook

[English](README.md) | [中文](README.zh-CN.md)

_此项目仍在开发中_

简单且易用的**静态**inline hook框架，支持**非越狱**iOS和macOS

## 使用

1. 在注入库中调用一次`stick_init()`
2. 用`stick_hook`或`stick_replace`宏声明hook补丁
3. 编译后，用`stickprep`工具把补丁打入目标二进制

## 文档

### stickhook 库

```c
int stick_init(void);
```

成功时返回 `0`

```c
void stick_hook(char *image_name, uint64_t vmaddr, void *replacement, void **originptr);
```

> `stick_hook`是使用C的宏实现的，这个声明仅作参考

声明一个函数的hook，并保存原函数指针

- `image_name` — 目标二进制的文件名
- `vmaddr` — 要hook的函数在二进制中的内存虚拟地址
- `replacement` — 用于hook的替换函数实现
- `originptr` — 保存原函数指针的指针

**注意：**同一个`image`的hook必须被**连续声明**

```c
void stick_replace(char *image_name, uint64_t vmaddr, void *replacement);
```

同上，但不保存原函数指针

### stickprep 工具

```bash
stickprep <library> <target>
```

> `library` 为注入库，`target` 为目标二进制（目标的文件名应与注入库中写的`image_name`相同）

把静态hook补丁写入目标二进制，并更新注入库中的运行时信息

如果注入库写了多个二进制的的hook，需要对每个二进制都运行一次`stickprep`

**注意：**`stickprep`会**同时修改**注入库和目标二进制，**不可**重复对同一个二进制打补丁

## 例子

见`test`目录
