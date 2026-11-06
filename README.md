# Ứng Dụng Chat Đa Luồng TCP/IP Trên Linux (Multi-Threaded TCP Chat Application)

Dự án này là bài tập thực hành chuyên sâu về **Lập trình hệ thống Linux (Linux Systems Programming)** và **Lập trình mạng (Network Socket Programming)** bằng ngôn ngữ C POSIX. 

Ứng dụng hiện thực một **Hệ thống phòng chat Client-Server qua giao thức TCP/IP** hoạt động hoàn toàn bằng **Mô hình Đa Luồng (Multi-threading)**, cho phép tối đa **10 người dùng kết nối và trò chuyện đồng thời**.

---

## 📋 Mục Lục
1. [Mục Tiêu & Yêu Cầu Đề Bài](#-mục-tiêu--yêu-cầu-đề-bài)
2. [Kiến Trúc Đa Luồng Trong Nhân Linux (Linux Threading Architecture)](#-kiến-trúc-đa-luồng-trong-nhân-linux)
3. [Cấu Trúc Thư Mục](#-cấu-trúc-thư-mục)
4. [Các Khái Niệm Lập Trình Linux Cốt Lõi](#-các-khái-niệm-lập-trình-linux-cốt-lõi)
   - [TGID (PID) vs TID (LWP)](#1-tgid-process-id-vs-tid-light-weight-process)
   - [Bản chất lời gọi clone() trong nhân](#2-bản-chất-của-pthread_create-lời-gọi-clone)
   - [Đồng bộ hóa & Race Condition (POSIX Mutex)](#3-đồng-bộ-hóa--phòng-ngừa-race-condition)
   - [Mô hình Client Full-Duplex 2 Luồng](#4-mô-hình-client-full-duplex-2-luồng)
5. [Giới Hạn Tối Đa 10 Người Dùng & Cơ Chế Từ Chối](#-giới-hạn-tối-đa-10-người-dùng--cơ-chế-từ-chối)
6. [Hướng Dẫn Biên Dịch & Chạy Ứng Dụng](#-hướng-dẫn-biên-dịch--chạy-ứng-dụng)
7. [Các Lệnh Kiểm Tra Luồng Trong Linux](#-các-lệnh-kiểm-tra-luồng-trong-linux)
8. [Bảng Lệnh Trong Phòng Chat](#-bảng-lệnh-trong-phòng-chat)
9. [Kiểm Thử Tự Động (Automated Testing)](#-kiểm-thử-tự-động)

---

## 🎯 Mục Tiêu & Yêu Cầu Đề Bài

- **Giao thức mạng**: TCP/IP (`AF_INET`, `SOCK_STREAM`) hướng kết nối tin cậy (reliable stream), đảm bảo thông điệp không bị mất mát hay đảo thứ tự.
- **Server điều phối (Coordinator)**:
  - 1 Server trung tâm lắng nghe trên cổng `8888`.
  - Phục vụ đồng thời nhiều client bằng **Multi-threading (POSIX pthreads)**.
  - Phân phối tin nhắn công khai (Broadcast), chuyển tiếp tin nhắn riêng tư (Private Direct Message), và theo dõi trạng thái người dùng.
- **Client**:
  - Giao diện dòng lệnh (CLI) tương tác mượt mà, hỗ trợ gửi và nhận tin nhắn hai chiều đồng thời (Full-Duplex) mà không bị treo I/O (`non-blocking feel`).
- **Giới hạn số lượng**: **Tối đa 10 clients** trò chuyện cùng lúc (`MAX_CLIENTS = 10`). Kết nối thứ 11 sẽ bị từ chối ngay lập tức kèm thông báo lỗi và đóng socket.

---

## 🏛️ Kiến Trúc Đa Luồng Trong Nhân Linux

```
[ SERVER PROCESS (PID / TGID = 12345) ]
 ├── [Main Listener Thread - TID: 12345] -> socket() -> bind() -> listen(8888) -> accept() loop
 │
 ├── [Worker Thread 1 - TID: 12346] <---> TCP Socket FD 4 <---> [Client 1: Alice]
 ├── [Worker Thread 2 - TID: 12347] <---> TCP Socket FD 5 <---> [Client 2: Bob]
 ├── [Worker Thread 3 - TID: 12348] <---> TCP Socket FD 6 <---> [Client 3: Charlie]
 │   ...
 └── [Worker Thread 10 - TID: 12355] <---> TCP Socket FD 13 <---> [Client 10: John]
        ▲
        │ Tất cả các Luồng chia sẻ chung:
        │  1. Vùng nhớ Heap, Global Variables (mảng clients[], client_count)
        │  2. Bảng mô tả tệp (File Descriptor Table - truy cập chéo socket của nhau)
        │  3. Được bảo vệ bởi pthread_mutex_t clients_mutex chống Race Condition!
```

Phía **Client Process**:
```
[ CLIENT PROCESS (PID / TGID = 12400) ]
 ├── [Send Thread - TID: 12401] ---> Đọc từ stdin (bàn phím) ---> send(sockfd)
 └── [Recv Thread - TID: 12402] <--- Nhận từ socket recv(sockfd) <--- In ra màn hình stdout
```

---

## 📁 Cấu Trúc Thư Mục

```
chat_application/
├── chat.h            # Header chung: hằng số MAX_CLIENTS 10, struct client_t, mã màu ANSI
├── server.c          # Server điều phối đa luồng (1 Listener Thread + N Worker Threads)
├── client.c          # Client đa luồng Full-Duplex (1 Send Thread + 1 Recv Thread)
├── Makefile          # Tự động biên dịch với gcc -Wall -Wextra -O2 -pthread
├── test_chat.sh      # Bash script tự động chạy kiểm thử
├── verify_chat.py    # Suite kiểm thử toàn diện 7 kịch bản (kết nối, broadcast, /threads, tràn 10 clients)
└── README.md         # Tài liệu học tập và hướng dẫn kỹ thuật chi tiết
```

---

## 🧠 Các Khái Niệm Lập Trình Linux Cốt Lõi

### 1. TGID (Process ID) vs TID (Light Weight Process)

Trong nhân Linux, **Process và Thread đều được biểu diễn bởi cùng một cấu trúc `struct task_struct`**:
- **TGID (Thread Group ID)**: Được trả về bởi hàm `getpid()`. Tất cả các luồng được sinh ra trong cùng một tiến trình đều có chung một TGID.
- **TID (Thread ID / LWP)**: Được trả về bởi lời gọi hệ thống `syscall(SYS_gettid)`. Mỗi luồng có một TID duy nhất trong toàn hệ thống.

Trong chương trình Server:
```c
pid_t server_pid = getpid();                     // TGID chung của Server
pid_t thread_tid = (pid_t)syscall(SYS_gettid);   // TID riêng của từng Worker Thread
```

### 2. Bản chất của `pthread_create()`: Lời gọi `clone()`

Khi gọi `pthread_create()`, thư viện NPTL trên Linux gọi đến system call `clone()` với các cờ chia sẻ tài nguyên:
```
clone(CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM)
```
- `CLONE_VM`: Các luồng dùng chung không gian địa chỉ bộ nhớ ảo.
- `CLONE_FILES`: Các luồng dùng chung **Bảng mô tả tệp (File Descriptor Table)**. Nhờ đó, Worker Thread của Alice có thể ghi dữ liệu trực tiếp vào socket FD của Bob để gửi tin nhắn!
- `CLONE_THREAD`: Gom các luồng vào cùng một Thread Group.

### 3. Đồng bộ hóa & Phòng ngừa Race Condition

Bởi vì mảng quản lý `clients[MAX_CLIENTS]` và biến đếm `client_count` nằm trong bộ nhớ chia sẻ chung (Shared Memory), khi nhiều Worker Thread đồng thời gửi tin nhắn hoặc thêm/xóa client, nếu không đồng bộ thì lệnh đọc-ghi sẽ bị xen kẽ dẫn đến hỏng bộ nhớ (**Race Condition**).

Giải pháp sử dụng **POSIX Mutex (`pthread_mutex_t`)**:
```c
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

static void broadcast_message(const char *msg, int sender_uid) {
    pthread_mutex_lock(&clients_mutex);    /* Bắt đầu Critical Section */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->uid != sender_uid) {
            send(clients[i]->sockfd, msg, strlen(msg), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);  /* Kết thúc Critical Section */
}
```

### 4. Mô hình Client Full-Duplex 2 Luồng

Nếu chỉ dùng 1 luồng đơn, hàm `fgets()` chờ nhập từ bàn phím sẽ chặn hoàn toàn tiến trình (`blocking I/O`). Người dùng sẽ không thể nhận được bất kỳ tin nhắn nào từ người khác cho đến khi bấm Enter.

Do đó, Client được chia thành **2 luồng độc lập**:
1. **Luồng đọc bàn phím (`send_msg_handler`)**: Chờ người dùng gõ tin nhắn và gửi tới server qua `send()`.
2. **Luồng đọc mạng (`recv_msg_handler`)**: Chờ dữ liệu từ server qua `recv()` và in ra màn hình ngay lập tức, sử dụng mã xóa dòng ANSI `\r\33[2K` để giữ dấu nhắc `> ` luôn gọn gàng.

---

## 🚫 Giới Hạn Tối Đa 10 Người Dùng & Cơ Chế Từ Chối

Server thiết lập hằng số `MAX_CLIENTS = 10`. Trong vòng lặp chính của Luồng Lắng Nghe (Main Listener Thread):

```c
pthread_mutex_lock(&clients_mutex);
if (client_count >= MAX_CLIENTS) {
    pthread_mutex_unlock(&clients_mutex);
    printf("[REJECTED] Connection from %s rejected (Maximum 10 clients reached).\n", ...);

    char *reject_msg = "[SERVER ERROR] Room is full (Maximum 10 clients reached). Connection rejected.\r\n";
    send(client_fd, reject_msg, strlen(reject_msg), 0);
    close(client_fd);  /* Đóng kết nối ngay lập tức, không spawn worker thread */
    continue;
}
pthread_mutex_unlock(&clients_mutex);
```

- Khi phòng đã đủ 10 người, kết nối thứ 11 sẽ nhận ngay thông báo lỗi và bị ngắt kết nối.
- Khi có bất kỳ ai trong phòng thoát (`/quit` hoặc mất mạng), slot trống lập tức được giải phóng để đón người dùng mới.

---

## 🚀 Hướng Dẫn Biên Dịch & Chạy Ứng Dụng

### Bước 1: Mở Terminal WSL / Ubuntu

```bash
cd /mnt/c/Users/khoa/Desktop/acad_linux/chat_application
```

### Bước 2: Biên dịch chương trình

```bash
make
```
Hệ thống sẽ biên dịch sạch sẽ không có cảnh báo:
- `./server`
- `./client`

*(Dọn dẹp file nhị phân: `make clean`)*

### Bước 3: Chạy Server điều phối

```bash
./server 8888
# Hoặc: make run-server
```

Màn hình Server hiển thị:
```
========================================================
  🚀 MULTI-THREADED LINUX TCP CHAT SERVER STARTED
  - Server Process PID (TGID) : 2450
  - Main Listener Thread (TID): 2450 (LWP)
  - Listening Port            : 8888
  - Max Clients Limit         : 10 concurrent users
  - Threading Architecture    : 1 Listener Thread + N Worker Threads
  - Live Thread Inspection    : ps -eLf -q 2450
  - Press Ctrl+C to gracefully stop the server
========================================================
```

### Bước 4: Chạy các Client trong các Terminal khác

Mở thêm 2 hoặc nhiều terminal khác và chạy:
```bash
./client 127.0.0.1 8888
# Hoặc: make run-client
```

Nhập tên người dùng (ví dụ: `Alice`, `Bob`):
```
Enter your username (2-30 characters): Alice
[CONNECTING] Connecting to server 127.0.0.1:8888...
=== Welcome to TCP Chat Application, Alice! ===
Commands: /list, /msg <user> <msg>, /threads, /help, /quit
Connected users: 1/10 (Worker Thread TID: 2455)
===============================================
> 
```

---

## 🔍 Các Lệnh Kiểm Tra Luồng Trong Linux

Trong khi Server và các Client đang chạy, bạn có thể mở một terminal khác để trực tiếp quan sát cách nhân Linux quản lý các luồng:

```bash
# 1. Liệt kê tất cả các Thread (LWP - Light Weight Process) của Server:
ps -eLf -q $(pgrep -f "^./server")

# 2. Xem cây tiến trình và các luồng phụ thuộc:
pstree -p $(pgrep -f "^./server")

# 3. Theo dõi hoạt động luồng thời gian thực:
top -H -p $(pgrep -f "^./server")

# 4. Kiểm tra thư mục task do nhân Linux cấp phát cho từng luồng:
ls -l /proc/$(pgrep -f "^./server")/task/

# 5. Xem số lượng luồng đang chạy:
cat /proc/$(pgrep -f "^./server")/status | grep Threads
```

---

## 💬 Bảng Lệnh Trong Phòng Chat

| Lệnh | Ý nghĩa | Ví dụ |
| :--- | :--- | :--- |
| `[tin nhắn]` | Gửi tin nhắn công khai cho tất cả mọi người | `Xin chào cả phòng!` |
| `/list` | Xem danh sách người dùng online và TID của Worker Thread | `/list` |
| `/msg <user> <text>` | Gửi tin nhắn riêng tư trực tiếp (Private Message) | `/msg Bob Bạn có rảnh không?` |
| `/threads` hoặc `/info` | Xem thông tin chẩn đoán luồng nhân Linux trực tiếp | `/threads` |
| `/help` | Hiển thị hướng dẫn sử dụng lệnh | `/help` |
| `/quit` hoặc `/exit` | Rời khỏi phòng chat an toàn | `/quit` |

---

## 🧪 Kiểm Thử Tự Động (Automated Testing)

Chạy bộ kiểm thử tự động toàn diện:
```bash
make test
# Hoặc: ./test_chat.sh
```

### 7 kịch bản kiểm thử được tự động thực hiện:
1. **Test 1**: Khởi động Server, thiết lập bắt tay và xác thực nhận banner chào mừng.
2. **Test 2**: Kết nối Client 2, xác thực thông báo broadcast khi có thành viên mới và kiểm tra gửi/nhận tin nhắn phòng chat.
3. **Test 3**: Kiểm tra lệnh `/list` trả về chính xác danh sách thành viên kèm Thread TID.
4. **Test 4**: Kiểm tra lệnh chẩn đoán `/threads` trả về đầy đủ Server PID, Handler TID và số luồng của nhân Linux.
5. **Test 5**: Kiểm tra tin nhắn riêng tư `/msg` chỉ gửi đến đúng người nhận và có tin xác nhận cho người gửi.
6. **Test 6 (Ràng buộc tối đa 10 người)**:
   - Kết nối liên tiếp **10 clients** cùng lúc (User1 đến User10).
   - Xác nhận cả 10 clients kết nối thành công và có 10 Worker Threads riêng biệt.
   - Thử kết nối **Client thứ 11** -> Server lập tức từ chối với thông điệp `"Room is full (Maximum 10 clients reached)"` và tự động đóng socket.
7. **Test 7 (Giải phóng slot và tái sử dụng)**:
   - Cho User1 thoát (`/quit`).
   - Kết nối Client mới (User11_New) vào slot vừa trống -> Kết nối thành công!
