# DMA-benchmark-rpi4



 project này là một bài Lab so sánh tốc độ sao chép bộ nhớ RAM-to-RAM giữa phương pháp sử dụng CPU truyền thống (memcpy()) và phương pháp sử dụng Bộ điều khiển truy cập bộ nhớ trực tiếp (DMA) trên Raspberry Pi 4.

DMA cho phép phần cứng tự thực hiện việc sao chép dữ liệu mà không cần làm phiền CPU, giải phóng CPU cho các tác vụ khác.





Mục Tiêu Bài Lab

So sánh tốc độ: Đo lường và so sánh thời gian thực hiện của memcpy() (CPU) và DMA (Phần cứng).

Hiểu về DMA: Thực hành giao tiếp với phần cứng DMA của Broadcom (BCM2711) thông qua đăng ký và Mailbox Kernel.

Công cụ đo lường: Sử dụng 2 chân GPIO để tạo xung (pulse) làm mốc thời gian, cho phép đo lường chính xác bằng Logic Analyzer bên ngoài.





Cấu Hình \& Yêu Cầu

Yêu cầu phần cứng

Raspberry Pi 4 

Logic Analyzer (Tùy chọn, để xem xung GPIO 17 \& 18).





Yêu cầu phần mềm

Hệ điều hành: Raspberry Pi OS (hoạt động tốt nhất).

Quyền hạn: Chương trình phải được chạy với quyền root (sử dụng sudo) để truy cập /dev/mem, /dev/vcio và GPIO.





link tài liệu:   https://docs.google.com/document/d/1JtbS2vMBXvNdPLcXgT6FZMWv53d32FApzKT1UJsHgFk/edit?usp=sharing

link video demo:   https://www.youtube.com/watch?v=XftPizQVmlU

Nguyen Quang Minh - 0916254336
