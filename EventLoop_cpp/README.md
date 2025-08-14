# EventLoop_cpp 构建与使用指南

一个基于 Linux epoll + eventfd 的 C++ 事件循环，实现统一的 I/O 事件与任务队列调度。
支持通过 CMake 选项可选集成第三方线程池（位于 `thirdpart/ZJPTools`）。

## 功能特性

- 统一事件循环：
  - I/O 事件（epoll）
  - 任务队列（事件唤醒epoll）
- 可选线程池：
  - 开启后，任务队列中的任务由线程池执行；
  - 关闭时，任务在事件线程执行（默认）。
- 优雅退出：`stop()` 唤醒并退出事件线程。

## 目录结构

- `include/EventLoop.h`：公共 API
- `src/EventLoop.cpp`：实现
- `examples/examples.cpp`：批量任务吞吐性能示例
- `thirdpart/ZJPTools/`：第三方线程池（已预安装头/库见 `install/`）

## 先决条件

- 操作系统：Linux
- 构建工具：CMake ≥ 3.10
- 编译器：支持 C++17
- 依赖：pthread

## CMake 选项

- `BUILD_SHARED_LIBS`：是否构建为共享库（默认 ON）
- `USE_ZJP_THREADPOOL`：是否启用第三方线程池（默认 OFF）
  - ON 时：
    - 自动包含 `thirdpart/ZJPTools/install/include`
    - 自动链接 `thirdpart/ZJPTools/install/lib/libzjpThreadloop.{a,so}`
    - 定义宏 `USE_ZJP_THREADPOOL`

## 构建步骤

在项目根目录 `EventLoop_cpp/` 执行：

### 0) 构建前准备（使用根目录 configure 脚本获取/安装线程池）
若需要启用线程池，请先在项目根目录执行 `./configure` 拉取并构建第三方线程池到 `thirdpart/ZJPTools/install/`：

```bash
# 默认从 https://github.com/mo-xiaoxiu/ZJPTools.git 克隆
./configure                # 清理并构建（首次推荐）

# 常用选项：
./configure --no-clean     # 跳过清理，加快重复构建
./configure --clear        # 清理 thirdpart/ 与 build/ 后退出
```

成功后应存在：
- 头文件：`thirdpart/ZJPTools/install/include/threadloop/ThreadLoop.h`
- 库文件：`thirdpart/ZJPTools/install/lib/libzjpThreadloop.{a,so}`

### 1) 不使用线程池（默认）
```bash
cmake -S . -B build -DUSE_ZJP_THREADPOOL=OFF
cmake --build build -j
```

### 2) 使用线程池
先执行上面的“0) 构建前准备”，确保 `thirdpart/ZJPTools/install/` 下已有可用的头/库文件：
- 头文件：`thirdpart/ZJPTools/install/include/threadloop/ThreadLoop.h`
- 库文件：`thirdpart/ZJPTools/install/lib/libzjpThreadloop.{a,so}`

然后执行：
```bash
cmake -S . -B build -DUSE_ZJP_THREADPOOL=ON
cmake --build build -j
```
> 说明：如使用共享库 `.so`，工程已将 RPATH 指向 `thirdpart/ZJPTools/install/lib`，在构建目录中直接运行示例无需额外 `LD_LIBRARY_PATH` 设置。

### （可选）重建第三方线程池
进入 `thirdpart/ZJPTools/` 执行：
```bash
./build_tools.sh --clean --build --install
```
安装产物会落到 `thirdpart/ZJPTools/install/`，随后按“使用线程池”方式构建本项目。

## 运行示例（吞吐性能测试）

可执行文件路径：`build/examples/example`

用法：
```bash
./build/examples/example [num_tasks] [work_iters]
```
- `num_tasks`：投递任务数量（默认 200000）
- `work_iters`：每个任务的 CPU 模拟工作量迭代次数（默认 0，不做额外计算）

示例：
```bash
# 仅测分发吞吐（无 CPU 负载）
./build/examples/example 200000 0

# 为每个任务增加 CPU 计算，观察线程池并行带来的加速
./build/examples/example 200000 200
```
程序会打印：是否启用线程池、完成总耗时与 tasks/s 吞吐。

## 快速 API 参考

- 事件循环：
  - `EventLoop::getInstance().run();`
  - `EventLoop::getInstance().stop();`  // 优雅退出
- 任务：
  - `addTask(EventCallback cb, void* arg);`
    - `USE_ZJP_THREADPOOL=ON`：任务在线程池执行
    - `USE_ZJP_THREADPOOL=OFF`：任务在事件线程执行
- I/O 监听（epoll 标志：`EPOLLIN/EPOLLOUT/EPOLLET/...`）：
  - `addIo(int fd, uint32_t events, IoCallback cb, void* arg);`
  - `modIo(int fd, uint32_t events);`
  - `delIo(int fd);`

## I/O 使用建议

- 对所有监听的 fd 使用非阻塞读写；
- 若使用 ET（边沿触发），务必循环 `read/write` 直到返回 `EAGAIN/EWOULDBLOCK`；
- I/O 回调要“短小非阻塞”，重活用 `addTask()` 投递（启用线程池后将在线程池执行）。

## 关闭与退出

- 调用 `stop()` 请求事件线程退出，内部通过 `eventfd` 唤醒 `epoll_wait()`；
- 析构中会 `join()` 事件线程；若线程池处于运行状态，也会调用其 `join()`。

## 故障排查

- 运行时报找不到 `libzjpThreadloop.so`：
  - 确认构建时启用了线程池且 `install/lib` 下存在 `.so`；
  - 直接在构建目录运行通常无需设置环境变量；若仍报错，可尝试：
    ```bash
    export LD_LIBRARY_PATH=$(pwd)/thirdpart/ZJPTools/install/lib:$LD_LIBRARY_PATH
    ```
- I/O 回调阻塞导致吞吐低：
  - 将耗时逻辑改为 `addTask()` 形式投递；或在回调中只解析数据，不做重活。

## 许可证

按仓库根目录中的许可证声明（如有）执行。
