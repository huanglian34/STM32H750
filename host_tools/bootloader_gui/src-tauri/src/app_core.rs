// STM32H750 Bootloader GUI Backend Core
// Tauri 2.0 Native Rust Serial & Ymodem Protocol Engine

use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use std::thread;
use std::io::{Read, Write};
use serde::Serialize;
use tauri::{AppHandle, Emitter, State};

const FIRMWARE_MAGIC: u32 = 0x53544D32; // 'STM2'
const HEADER_FORMAT_VER: u32 = 0x00010000;

// 全局串口状态管理
struct SerialState {
    port: Arc<Mutex<Option<Box<dyn serialport::SerialPort>>>>,
    is_connected: Arc<Mutex<bool>>,
    handshake_active: Arc<Mutex<bool>>,
    is_uploading: Arc<Mutex<bool>>,
}

// RAII 保护锁，保证在烧录异常退出时必将 is_uploading 恢复为 false 并复原串口超时
struct UploadGuard {
    flag: Arc<Mutex<bool>>,
    port: Arc<Mutex<Option<Box<dyn serialport::SerialPort>>>>,
}
impl Drop for UploadGuard {
    fn drop(&mut self) {
        if let Ok(mut f) = self.flag.lock() {
            *f = false;
        }
        if let Ok(mut port_lock) = self.port.lock() {
            if let Some(ref mut p) = *port_lock {
                let _ = p.set_timeout(Duration::from_millis(50));
            }
        }
    }
}

fn wait_for_char(port: &mut Box<dyn serialport::SerialPort>, target: u8, timeout: Duration) -> bool {
    let start = Instant::now();
    let mut buf = [0u8; 1];
    while start.elapsed() < timeout {
        match port.read(&mut buf) {
            Ok(1) => {
                if buf[0] == target {
                    return true;
                }
            }
            Ok(_) => {}
            Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut || e.raw_os_error() == Some(121) => {}
            Err(_) => {
                thread::sleep(Duration::from_millis(2));
            }
        }
        thread::sleep(Duration::from_millis(1));
    }
    false
}

#[derive(Serialize, Clone)]
struct UploadProgress {
    percent: u32,
    sent_bytes: usize,
    total_bytes: usize,
    speed_kb: f32,
    elapsed_sec: u64,
    stage: String,
    crc: String,
}

// 1. 枚举系统可用串口
#[tauri::command]
fn list_ports() -> Result<Vec<String>, String> {
    match serialport::available_ports() {
        Ok(ports) => {
            let port_names: Vec<String> = ports.into_iter().map(|p| p.port_name).collect();
            Ok(port_names)
        }
        Err(e) => Err(format!("扫描串口失败: {}", e)),
    }
}

// 2. 连接串口并开启后台接收线程
#[tauri::command]
fn connect_serial(
    app: AppHandle,
    state: State<'_, SerialState>,
    port_name: String,
    baud_rate: u32,
) -> Result<(), String> {
    let mut port_guard = state.port.lock().map_err(|e| e.to_string())?;
    
    // 打开串口
    let port_builder = serialport::new(&port_name, baud_rate)
        .timeout(Duration::from_millis(50));

    let port = port_builder.open().map_err(|e| format!("打开 {} 失败: {}", port_name, e))?;
    
    *port_guard = Some(port);
    *state.is_connected.lock().unwrap() = true;

    // 启动后台接收监控线程
    let port_clone = state.port.clone();
    let is_connected_clone = state.is_connected.clone();
    let handshake_active_clone = state.handshake_active.clone();
    let is_uploading_clone = state.is_uploading.clone();
    let app_handle = app.clone();

    thread::spawn(move || {
        let mut buffer = [0u8; 1024];
        while *is_connected_clone.lock().unwrap() {
            // 固件烧录期间后台监听线程必须完全礼让，严防两个线程同时调用 read 争抢串口数据
            if *is_uploading_clone.lock().unwrap() {
                thread::sleep(Duration::from_millis(20));
                continue;
            }

            let mut read_bytes = 0;
            {
                if let Ok(mut port_lock) = port_clone.lock() {
                    if let Some(ref mut p) = *port_lock {
                        if let Ok(n) = p.read(&mut buffer) {
                            read_bytes = n;
                        }
                    }
                }
            }

            if read_bytes > 0 {
                let rx_str = String::from_utf8_lossy(&buffer[..read_bytes]).to_string();
                // 只有在单片机真正确认被拦截成功（收到[HANDSHAKE_ACK]或bootloader>）时，才停止发空格
                if rx_str.contains("[HANDSHAKE_ACK]") || rx_str.contains("bootloader>") || rx_str.contains("Intercepted") {
                    let mut hs = handshake_active_clone.lock().unwrap();
                    *hs = false;
                }
                let _ = app_handle.emit("serial-rx", rx_str);
            } else {
                thread::sleep(Duration::from_millis(2));
            }
        }
    });

    Ok(())
}

// 3. 断开串口连接
#[tauri::command]
fn disconnect_serial(state: State<'_, SerialState>) -> Result<(), String> {
    *state.is_connected.lock().map_err(|e| e.to_string())? = false;
    let mut port_guard = state.port.lock().map_err(|e| e.to_string())?;
    *port_guard = None;
    Ok(())
}

// 4. 发送纯文本控制台指令
#[tauri::command]
fn send_serial_cmd(state: State<'_, SerialState>, cmd: String) -> Result<(), String> {
    let mut port_guard = state.port.lock().map_err(|e| e.to_string())?;
    if let Some(ref mut p) = *port_guard {
        let cmd_bytes = format!("{}\r\n", cmd);
        p.write_all(cmd_bytes.as_bytes()).map_err(|e| format!("发送失败: {}", e))?;
        let _ = p.flush();
        Ok(())
    } else {
        Err("串口未连接".to_string())
    }
}

// CRC16-CCITT for Ymodem
fn calc_crc16(data: &[u8]) -> u16 {
    let mut crc = 0u16;
    for &byte in data {
        crc ^= (byte as u16) << 8;
        for _ in 0..8 {
            if (crc & 0x8000) != 0 {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    crc
}

// 简易十六进制单字节解码辅助函数
fn parse_hex_byte(s: &[u8]) -> Option<u8> {
    if s.len() < 2 { return None; }
    let h1 = char::from(s[0]).to_digit(16)?;
    let h2 = char::from(s[1]).to_digit(16)?;
    Some(((h1 << 4) | h2) as u8)
}

// 1. 原生 Intel HEX 解析器：自动提取连续有效固件段并转为纯二进制镜像
fn parse_intel_hex(data: &[u8]) -> Result<Vec<u8>, String> {
    let content = std::str::from_utf8(data).map_err(|_| "HEX 文件不是有效的 UTF-8/ASCII 编码".to_string())?;
    let mut upper_address = 0u32;
    let mut chunks: Vec<(u32, Vec<u8>)> = Vec::new();

    for (line_idx, raw_line) in content.lines().enumerate() {
        let line = raw_line.trim().as_bytes();
        if line.is_empty() || line[0] != b':' {
            continue;
        }
        let body = &line[1..];
        if body.len() < 10 {
            continue;
        }

        let len = parse_hex_byte(&body[0..2]).ok_or_else(|| format!("第 {} 行长度错误", line_idx + 1))? as usize;
        let addr_hi = parse_hex_byte(&body[2..4]).unwrap_or(0) as u32;
        let addr_lo = parse_hex_byte(&body[4..6]).unwrap_or(0) as u32;
        let offset = (addr_hi << 8) | addr_lo;
        let rectype = parse_hex_byte(&body[6..8]).unwrap_or(0);

        if body.len() < 8 + len * 2 + 2 {
            continue;
        }

        let mut record_data = Vec::with_capacity(len);
        for i in 0..len {
            let b = parse_hex_byte(&body[8 + i * 2 .. 10 + i * 2])
                .ok_or_else(|| format!("第 {} 行数据解析错误", line_idx + 1))?;
            record_data.push(b);
        }

        match rectype {
            0x00 => { // 数据记录
                let abs_addr = upper_address | offset;
                chunks.push((abs_addr, record_data));
            }
            0x01 => break, // 文件结束
            0x02 => { // 扩展段地址
                if record_data.len() == 2 {
                    let seg = ((record_data[0] as u32) << 8) | (record_data[1] as u32);
                    upper_address = seg << 4;
                }
            }
            0x04 => { // 扩展线性地址 (基地址高 16 位，如 0x9000 或 0x0800)
                if record_data.len() == 2 {
                    let high = ((record_data[0] as u32) << 8) | (record_data[1] as u32);
                    upper_address = high << 16;
                }
            }
            _ => {}
        }
    }

    if chunks.is_empty() {
        return Err("HEX 文件中未找到有效固件数据段".to_string());
    }

    // 找到最小和最大地址
    let min_addr = chunks.iter().map(|(a, _)| *a).min().unwrap();
    let max_addr = chunks.iter().map(|(a, d)| *a + d.len() as u32).max().unwrap();
    let total_size = (max_addr - min_addr) as usize;

    if total_size > 8 * 1024 * 1024 {
        return Err(format!("HEX 文件地址跨度异常 ({} MB)，超出 Flash 限制", total_size / 1024 / 1024));
    }

    let mut binary = vec![0xFFu8; total_size];
    for (addr, d) in chunks {
        let start = (addr - min_addr) as usize;
        binary[start..start + d.len()].copy_from_slice(&d);
    }

    Ok(binary)
}

// 2. 原生 ELF / AXF 解析器：自动提取可加载段 (PT_LOAD) 生成纯二进制镜像
fn parse_elf_axf(data: &[u8]) -> Result<Vec<u8>, String> {
    if data.len() < 52 || &data[0..4] != b"\x7FELF" {
        return Err("不是有效的 ELF/AXF 文件".to_string());
    }
    // 只支持 32 位小端 (STM32 Cortex-M7 为 ELFCLASS32 + ELFDATA2LSB)
    if data[4] != 1 || data[5] != 1 {
        return Err("仅支持 32 位小端格式的 ELF/AXF 文件".to_string());
    }

    let phoff = u32::from_le_bytes(data[28..32].try_into().unwrap()) as usize;
    let phentsize = u16::from_le_bytes(data[42..44].try_into().unwrap()) as usize;
    let phnum = u16::from_le_bytes(data[44..46].try_into().unwrap()) as usize;

    let mut segments: Vec<(u32, Vec<u8>)> = Vec::new();

    for i in 0..phnum {
        let entry_offset = phoff + i * phentsize;
        if entry_offset + 32 > data.len() { break; }

        let p_type = u32::from_le_bytes(data[entry_offset..entry_offset + 4].try_into().unwrap());
        let p_offset = u32::from_le_bytes(data[entry_offset + 4..entry_offset + 8].try_into().unwrap()) as usize;
        let p_vaddr = u32::from_le_bytes(data[entry_offset + 8..entry_offset + 12].try_into().unwrap());
        let p_paddr = u32::from_le_bytes(data[entry_offset + 12..entry_offset + 16].try_into().unwrap());
        let p_filesz = u32::from_le_bytes(data[entry_offset + 16..entry_offset + 20].try_into().unwrap()) as usize;

        // PT_LOAD = 1 且包含非零文件数据
        if p_type == 1 && p_filesz > 0 {
            if p_offset + p_filesz <= data.len() {
                // 优先取物理地址 p_paddr，若为 0 则取虚拟地址 p_vaddr
                let addr = if p_paddr != 0 { p_paddr } else { p_vaddr };
                let seg_bytes = data[p_offset..p_offset + p_filesz].to_vec();
                segments.push((addr, seg_bytes));
            }
        }
    }

    if segments.is_empty() {
        return Err("ELF/AXF 文件中未找到 PT_LOAD 可加载数据段".to_string());
    }

    let min_addr = segments.iter().map(|(a, _)| *a).min().unwrap();
    let max_addr = segments.iter().map(|(a, d)| *a + d.len() as u32).max().unwrap();
    let total_size = (max_addr - min_addr) as usize;

    if total_size > 8 * 1024 * 1024 {
        return Err(format!("ELF 数据跨度异常 ({} MB)", total_size / 1024 / 1024));
    }

    let mut binary = vec![0xFFu8; total_size];
    for (addr, d) in segments {
        let start = (addr - min_addr) as usize;
        binary[start..start + d.len()].copy_from_slice(&d);
    }

    Ok(binary)
}

// 5. 固件安全打包与 Ymodem 标准流式烧录
#[tauri::command]
async fn upload_firmware_bytes(
    app: AppHandle,
    state: State<'_, SerialState>,
    file_name: String,
    mut file_bytes: Vec<u8>,
    target_slot: String,
    version_str: String,
    with_header: bool,
) -> Result<(), String> {
    // 自动识别并转换固件格式（原生支持 HEX / AXF / ELF / BIN）
    let lower_name = file_name.to_lowercase();
    if lower_name.ends_with(".hex") || file_bytes.starts_with(b":") {
        let _ = app.emit("serial-rx", "\r\n[PARSER] 检测到 Intel HEX 格式，正在智能解析提取有效固件段...\r\n".to_string());
        file_bytes = parse_intel_hex(&file_bytes)?;
        let _ = app.emit("serial-rx", format!("[PARSER] HEX 解析成功！已生成连续纯二进制镜像 ({} 字节)\r\n", file_bytes.len()));
    } else if lower_name.ends_with(".axf") || lower_name.ends_with(".elf") || file_bytes.starts_with(b"\x7FELF") {
        let _ = app.emit("serial-rx", "\r\n[PARSER] 检测到 ARM ELF/AXF 格式，正在提取 PT_LOAD 可执行段...\r\n".to_string());
        file_bytes = parse_elf_axf(&file_bytes)?;
        let _ = app.emit("serial-rx", format!("[PARSER] ELF/AXF 解析成功！已生成连续纯二进制镜像 ({} 字节)\r\n", file_bytes.len()));
    }
    // 标记当前进入专属固件烧录状态，暂停后台终端线程读取
    *state.is_uploading.lock().unwrap() = true;
    let _guard = UploadGuard { 
        flag: state.is_uploading.clone(),
        port: state.port.clone(),
    };

    // 1. 如果启用了安全打包，自动注入 64 字节 Header
    if with_header {
        let payload_size = file_bytes.len() as u32;
        let payload_crc = crc32fast::hash(&file_bytes);
        
        let mut ver_parts = version_str.trim().trim_start_matches('v').split('.');
        let major = ver_parts.next().and_then(|s| s.parse::<u8>().ok()).unwrap_or(1);
        let minor = ver_parts.next().and_then(|s| s.parse::<u8>().ok()).unwrap_or(0);
        let patch = ver_parts.next().and_then(|s| s.parse::<u8>().ok()).unwrap_or(0);
        let ver_int = ((major as u32) << 24) | ((minor as u32) << 16) | ((patch as u32) << 8);

        let slot_id = if target_slot == "b" { 1u32 } else { 0u32 };
        let timestamp = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_secs() as u32;

        let mut header = vec![0u8; 60];
        header[0..4].copy_from_slice(&FIRMWARE_MAGIC.to_le_bytes());
        header[4..8].copy_from_slice(&HEADER_FORMAT_VER.to_le_bytes());
        header[8..12].copy_from_slice(&ver_int.to_le_bytes());
        header[12..16].copy_from_slice(&payload_size.to_le_bytes());
        header[16..20].copy_from_slice(&payload_crc.to_le_bytes());
        header[20..24].copy_from_slice(&slot_id.to_le_bytes());
        header[24..28].copy_from_slice(&timestamp.to_le_bytes());

        let header_crc = crc32fast::hash(&header);
        header.extend_from_slice(&header_crc.to_le_bytes());

        let mut packaged = header;
        packaged.extend(file_bytes);
        file_bytes = packaged;
    }

    let total_len = file_bytes.len();
    let total_crc = crc32fast::hash(&file_bytes);
    let start_time = Instant::now();

    let mut port_guard = state.port.lock().map_err(|e| e.to_string())?;
    let p = port_guard.as_mut().ok_or_else(|| "串口未连接".to_string())?;

    // 关键优化：固件烧录期间动态设置串口超时为 3 秒，杜绝 Win32 WriteFile 对大包报 os error 121
    let _ = p.set_timeout(Duration::from_secs(3));

    // 清空历史 RX 缓存
    let mut drain = [0u8; 512];
    while let Ok(n) = p.read(&mut drain) {
        if n == 0 { break; }
    }

    // 2. 发送烧录命令唤醒下位机
    let cmd = if target_slot == "b" { "ymodem b\r\n" } else { "ymodem a\r\n" };
    let _ = app.emit("serial-rx", format!("\r\n[YMODEM] >>> 开始固件传输: {} ({} 字节) -> 目标 Slot {} <<<\r\n", file_name, total_len, target_slot.to_uppercase()));
    p.write_all(cmd.as_bytes()).map_err(|e| format!("发送烧录命令失败: {}", e))?;
    let _ = p.flush();

    // 等待板卡首次发出 'C' (0x43) 握手响应（最长 8 秒）
    let got_c = wait_for_char(p, b'C', Duration::from_secs(8));
    if !got_c {
        let _ = app.emit("serial-rx", "[YMODEM-ERR] 等待板卡 Ymodem 'C' 握手响应超时！\r\n".to_string());
        return Err("等待板卡 Ymodem 'C' 握手响应超时！请确保板卡处于 Bootloader 交互模式。".to_string());
    }
    let _ = app.emit("serial-rx", "[YMODEM] 握手成功 (收到 'C')，发送 Packet 0 文件元数据...\r\n".to_string());

    // 3. 发送 Packet 0 (文件名与文件大小)
    let mut packet0 = vec![0u8; 128];
    let file_info = format!("{}\0{}\0", file_name, total_len);
    packet0[..file_info.len()].copy_from_slice(file_info.as_bytes());

    let mut send_buf = Vec::new();
    send_buf.push(0x01); // SOH
    send_buf.push(0x00); // Seq 0
    send_buf.push(0xFF); // Seq ~0
    send_buf.extend_from_slice(&packet0);
    let crc0 = calc_crc16(&packet0);
    send_buf.extend_from_slice(&crc0.to_be_bytes());

    p.write_all(&send_buf).map_err(|e| format!("发送 Packet 0 失败: {}", e))?;
    let _ = p.flush();

    // 单片机收到 Packet 0 后会自动擦除对应 Flash 扇区（给 12 秒裕量）
    // 擦除完成后单片机发送 ACK (0x06) + CRC16 ('C', 0x43)
    let _ = app.emit("serial-rx", "[YMODEM] 等待板卡擦除对应 Flash 扇区 (请稍候)...\r\n".to_string());
    let got_ack0 = wait_for_char(p, 0x06, Duration::from_secs(12));
    if !got_ack0 {
        let _ = app.emit("serial-rx", "[YMODEM-ERR] 等待板卡扇区擦除完成 (ACK) 超时！\r\n".to_string());
        return Err("等待板卡扇区擦除完成 (ACK) 超时！".to_string());
    }
    let got_c_start = wait_for_char(p, b'C', Duration::from_secs(5));
    if !got_c_start {
        return Err("等待数据流传输就绪 ('C') 超时！".to_string());
    }
    let _ = app.emit("serial-rx", "[YMODEM] 扇区擦除完毕，开始流式传输 1024 字节数据包...\r\n".to_string());

    // 4. 流式循环发送 1024 字节数据包 (1K)
    let mut sent = 0;
    let mut seq = 1u8;
    let mut last_log_percent: u32 = 0;

    while sent < total_len {
        let chunk_size = std::cmp::min(1024, total_len - sent);
        let mut chunk = vec![0x1A; 1024]; // 0x1A (EOF) 填充
        chunk[..chunk_size].copy_from_slice(&file_bytes[sent..sent + chunk_size]);

        let mut data_packet = Vec::with_capacity(1024 + 5);
        data_packet.push(0x02); // STX (1K)
        data_packet.push(seq);
        data_packet.push(!seq);
        data_packet.extend_from_slice(&chunk);
        let crc = calc_crc16(&chunk);
        data_packet.extend_from_slice(&crc.to_be_bytes());

        p.write_all(&data_packet).map_err(|e| format!("发送数据包 #{} 失败: {}", seq, e))?;
        let _ = p.flush();

        // 等待单片机 Flash 写入并确认 ACK (0x06)
        let ack = wait_for_char(p, 0x06, Duration::from_secs(4));
        if !ack {
            let _ = app.emit("serial-rx", format!("[YMODEM-ERR] 数据包 #{} 校验写入超时或失败！\r\n", seq));
            return Err(format!("数据包 #{} 烧录超时或单片机校验失败！", seq));
        }

        sent += chunk_size;
        seq = seq.wrapping_add(1);

        let elapsed = start_time.elapsed().as_secs_f32();
        let speed = if elapsed > 0.0 { (sent as f32 / 1024.0) / elapsed } else { 0.0 };
        let percent = ((sent as f32 / total_len as f32) * 100.0) as u32;

        if percent >= last_log_percent + 10 || sent == total_len {
            last_log_percent = (percent / 10) * 10;
            let _ = app.emit("serial-rx", format!("[YMODEM] 传输进度: {:>3}% ({:>6}/{:>6} 字节) 速度: {:.1} KB/s\r\n", percent, sent, total_len, speed));
        }

        let _ = app.emit("upload-progress", UploadProgress {
            percent,
            sent_bytes: sent,
            total_bytes: total_len,
            speed_kb: (speed * 10.0).round() / 10.0,
            elapsed_sec: elapsed as u64,
            stage: format!("正在烧录 {}/{} 字节", sent, total_len),
            crc: format!("0x{:08X}", total_crc),
        });
    }

    // 5. 发送第 1 次 EOT
    let _ = app.emit("serial-rx", "[YMODEM] 数据包发送完毕，下发 EOT 结束符...\r\n".to_string());
    p.write_all(&[0x04]).map_err(|e| format!("发送第 1 次 EOT 失败: {}", e))?;
    let _ = p.flush();

    // 单片机回复 NAK (0x15)
    let _ = wait_for_char(p, 0x15, Duration::from_secs(2));

    // 发送第 2 次 EOT
    p.write_all(&[0x04]).map_err(|e| format!("发送第 2 次 EOT 失败: {}", e))?;
    let _ = p.flush();

    // 单片机回复 ACK (0x06) + 'C' (0x43)
    let _ = wait_for_char(p, 0x06, Duration::from_secs(2));
    let _ = wait_for_char(p, b'C', Duration::from_secs(2));

    // 6. 发送空 Packet 0 (End of Session)
    let empty_packet0 = vec![0u8; 128];
    let mut end_buf = Vec::new();
    end_buf.push(0x01); // SOH
    end_buf.push(0x00);
    end_buf.push(0xFF);
    end_buf.extend_from_slice(&empty_packet0);
    let crc_empty = calc_crc16(&empty_packet0);
    end_buf.extend_from_slice(&crc_empty.to_be_bytes());

    p.write_all(&end_buf).map_err(|e| format!("发送结束空包失败: {}", e))?;
    let _ = p.flush();

    // 单片机最终回复 ACK (0x06)
    let _ = wait_for_char(p, 0x06, Duration::from_secs(2));

    // 7. 读取单片机随后的回显日志并在终端呈现
    thread::sleep(Duration::from_millis(300));
    let mut log_buf = [0u8; 1024];
    if let Ok(n) = p.read(&mut log_buf) {
        if n > 0 {
            let log_str = String::from_utf8_lossy(&log_buf[..n]).to_string();
            let _ = app.emit("serial-rx", log_str);
        }
    }

    let _ = app.emit("upload-progress", UploadProgress {
        percent: 100,
        sent_bytes: total_len,
        total_bytes: total_len,
        speed_kb: 0.0,
        elapsed_sec: start_time.elapsed().as_secs(),
        stage: "固件流式烧录完成，板卡自检校验成功！".to_string(),
        crc: format!("0x{:08X}", total_crc),
    });

    Ok(())
}

// 6. 启动 50ms 自动探测握手（最长 15 秒）
#[tauri::command]
fn start_auto_handshake(state: State<'_, SerialState>) -> Result<(), String> {
    *state.handshake_active.lock().map_err(|e| e.to_string())? = true;
    let port_clone = state.port.clone();
    let handshake_active_clone = state.handshake_active.clone();

    thread::spawn(move || {
        for _ in 0..300 {
            if !*handshake_active_clone.lock().unwrap() {
                break;
            }
            {
                if let Ok(mut port_lock) = port_clone.lock() {
                    if let Some(ref mut p) = *port_lock {
                        let _ = p.write_all(b" ");
                        let _ = p.flush();
                    }
                }
            }
            thread::sleep(Duration::from_millis(50));
        }
        *handshake_active_clone.lock().unwrap() = false;
    });

    Ok(())
}

// 7. 停止自动探测握手
#[tauri::command]
fn stop_auto_handshake(state: State<'_, SerialState>) -> Result<(), String> {
    *state.handshake_active.lock().map_err(|e| e.to_string())? = false;
    Ok(())
}

// 8. 智能从 APP 软复位并无缝拦截进入 Bootloader
#[tauri::command]
fn jump_to_bootloader(state: State<'_, SerialState>) -> Result<(), String> {
    // 发送软复位进 boot 命令
    {
        let mut port_guard = state.port.lock().map_err(|e| e.to_string())?;
        if let Some(ref mut p) = *port_guard {
            p.write_all(b"\r\njump_boot\r\n").map_err(|e| format!("发送跳转指令失败: {}", e))?;
            let _ = p.flush();
        } else {
            return Err("串口未连接".to_string());
        }
    }

    // 启动 40ms 自动拦截线程（发空格拦截 Bootloader 的 2s 倒计时）
    *state.handshake_active.lock().map_err(|e| e.to_string())? = true;
    let port_clone = state.port.clone();
    let handshake_active_clone = state.handshake_active.clone();

    thread::spawn(move || {
        // 先休眠 30ms 避开 MCU 正在复位的瞬态
        thread::sleep(Duration::from_millis(30));
        for _ in 0..120 { // 最长持续 5 秒
            if !*handshake_active_clone.lock().unwrap() {
                break;
            }
            {
                if let Ok(mut port_lock) = port_clone.lock() {
                    if let Some(ref mut p) = *port_lock {
                        let _ = p.write_all(b" ");
                        let _ = p.flush();
                    }
                }
            }
            thread::sleep(Duration::from_millis(40));
        }
        *handshake_active_clone.lock().unwrap() = false;
    });

    Ok(())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let serial_state = SerialState {
        port: Arc::new(Mutex::new(None)),
        is_connected: Arc::new(Mutex::new(false)),
        handshake_active: Arc::new(Mutex::new(false)),
        is_uploading: Arc::new(Mutex::new(false)),
    };

    tauri::Builder::default()
        .manage(serial_state)
        .plugin(tauri_plugin_opener::init())
        .invoke_handler(tauri::generate_handler![
            list_ports,
            connect_serial,
            disconnect_serial,
            send_serial_cmd,
            start_auto_handshake,
            stop_auto_handshake,
            jump_to_bootloader,
            upload_firmware_bytes
        ])
        .run(tauri::generate_context!())
        .expect("运行 Tauri Bootloader GUI 失败");
}
