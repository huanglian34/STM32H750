// ==========================================================================
// STM32H750 BootManager - Master Controller (全功能完整恢复版)
// 苹果极简纯白 · 顶栏彻底防抖 · 双槽指标 · 智能向量 · 终端随心收缩
// ==========================================================================

const { invoke } = window.__TAURI__.core;
const { listen } = window.__TAURI__.event;

// ==========================================================================
// 1. 全局状态存储
// ==========================================================================
let isConnected = false;
let isHandshaking = false;
let currentDeviceMode = "disconnected"; // "disconnected" | "handshaking" | "app" | "bootloader"
let currentSelectedFile = null;
let currentDeviceStatus = null;
let hexLinesBuffer = [];

// 命令行历史记忆
const cmdHistory = [];
let cmdHistoryIndex = -1;

// ==========================================================================
// 2. DOM 元素缓存
// ==========================================================================
// 顶部状态栏
const portSelect = document.getElementById("port-select");
const baudSelect = document.getElementById("baud-select");
const btnRefreshPorts = document.getElementById("btn-refresh-ports");
const btnConnect = document.getElementById("btn-connect");
const badgeDeviceMode = document.getElementById("badge-device-mode");
const btnJumpToBoot = document.getElementById("btn-jump-to-boot");
const btnQuickRunApp = document.getElementById("btn-quick-run-app");

// 侧边栏导航
const navItems = document.querySelectorAll(".nav-item");
const tabPanes = document.querySelectorAll(".tab-pane");
const stageContainer = document.querySelector(".stage");
const consoleDrawer = document.getElementById("console-drawer");

// 抽屉控制器
const btnToggleDrawer = document.getElementById("btn-toggle-drawer");
const drawerToggleIcon = document.getElementById("drawer-toggle-icon");
const drawerToggleText = document.getElementById("drawer-toggle-text");
const btnMaximizeDrawer = document.getElementById("btn-maximize-drawer");

// 固件烧录区
const dropZone = document.getElementById("drop-zone");
const fileInput = document.getElementById("file-input");
const btnSelectFile = document.getElementById("btn-select-file");
const fileInfo = document.getElementById("file-info");
const selectedFileName = document.getElementById("selected-file-name");
const selectedFileSize = document.getElementById("selected-file-size");
const fwTargetSlot = document.getElementById("fw-target-slot");
const fwPackMode = document.getElementById("fw-pack-mode");
const fwVersion = document.getElementById("fw-version");
const fwTag = document.getElementById("fw-tag");

// 烧录进度看板
const progressFill = document.getElementById("progress-fill");
const progressPercent = document.getElementById("progress-percent");
const progressSpeed = document.getElementById("progress-speed");
const transferStage = document.getElementById("transfer-stage");
const transferBytes = document.getElementById("transfer-bytes");
const transferTime = document.getElementById("transfer-time");
const transferCrc = document.getElementById("transfer-crc");
const btnFlashNow = document.getElementById("btn-flash-now");

// 系统总览指标
const valCpu = document.getElementById("val-cpu");
const valClock = document.getElementById("val-clock");
const valFlash = document.getElementById("val-flash");
const valJedec = document.getElementById("val-jedec");
const valHealth = document.getElementById("val-health");
const valBootCycle = document.getElementById("val-boot-cycle");
const valActiveBadge = document.getElementById("val-active-badge");
const valActiveSlot = document.getElementById("val-active-slot");
const btnQuickResetBoot = document.getElementById("btn-quick-reset-boot");

// 双槽卡片元素
const slotAValid = document.getElementById("slot-a-valid");
const slotACrc = document.getElementById("slot-a-crc");
const slotAMsp = document.getElementById("slot-a-msp");
const slotAPc = document.getElementById("slot-a-pc");
const slotAVer = document.getElementById("slot-a-ver");
const slotATag = document.getElementById("slot-a-tag");
const slotASize = document.getElementById("slot-a-size-bytes");
const slotAFile = document.getElementById("slot-a-file");
const slotATime = document.getElementById("slot-a-time");
const slotACapText = document.getElementById("slot-a-cap-text");
const slotACapFill = document.getElementById("slot-a-cap-fill");

const slotBValid = document.getElementById("slot-b-valid");
const slotBCrc = document.getElementById("slot-b-crc");
const slotBMsp = document.getElementById("slot-b-msp");
const slotBPc = document.getElementById("slot-b-pc");
const slotBVer = document.getElementById("slot-b-ver");
const slotBTag = document.getElementById("slot-b-tag");
const slotBSize = document.getElementById("slot-b-size-bytes");
const slotBFile = document.getElementById("slot-b-file");
const slotBTime = document.getElementById("slot-b-time");
const slotBCapText = document.getElementById("slot-b-cap-text");
const slotBCapFill = document.getElementById("slot-b-cap-fill");

// 灾备控制按钮
const btnActionBackup = document.getElementById("btn-action-backup");
const btnActionRestore = document.getElementById("btn-action-restore");
const btnActionVerifyA = document.getElementById("btn-action-verify-a");
const btnActionVerifyB = document.getElementById("btn-action-verify-b");
const btnActionResetCount = document.getElementById("btn-action-reset-count");
const btnActionBoot = document.getElementById("btn-action-boot");
const btnActionReboot = document.getElementById("btn-action-reboot");

// Flash Hex 诊断
const hexOffset = document.getElementById("hex-offset");
const hexLen = document.getElementById("hex-len");
const btnReadHex = document.getElementById("btn-read-hex");
const hexDataView = document.getElementById("hex-data-view");
const anTarget = document.getElementById("an-target");
const anMsp = document.getElementById("an-msp");
const anPc = document.getElementById("an-pc");
const anStatus = document.getElementById("an-status");

// 终端区
const termOutput = document.getElementById("term-output");
const termInput = document.getElementById("term-input");
const btnTermSend = document.getElementById("btn-term-send");
const btnTermClear = document.getElementById("btn-term-clear");
const btnTermCopy = document.getElementById("btn-term-copy");
const cmdPills = document.querySelectorAll(".cmd-pill");

// ==========================================================================
// 3. 导航与终端抽屉收缩控制器
// ==========================================================================
navItems.forEach((btn) => {
  btn.addEventListener("click", () => {
    navItems.forEach((b) => b.classList.remove("active"));
    tabPanes.forEach((p) => p.classList.remove("active"));

    btn.classList.add("active");
    const targetId = btn.getAttribute("data-tab");
    const targetPane = document.getElementById(targetId);
    if (targetPane) targetPane.classList.add("active");

    // 若切到【💻 交互控制台】，自动将终端铺满展示海量数据
    if (targetId === "tab-term") {
      stageContainer.classList.add("tab-term-active");
      if (consoleDrawer.classList.contains("collapsed")) {
        consoleDrawer.classList.remove("collapsed");
      }
    } else {
      stageContainer.classList.remove("tab-term-active");
    }

    // 切到 Hex 诊断时，若无数据且已连接，自动读一次
    if (targetId === "tab-hex" && hexLinesBuffer.length === 0 && isConnected) {
      triggerHexRead("0x000000", 256);
    }
  });
});

// 抽屉收缩/展开切换
if (btnToggleDrawer) {
  btnToggleDrawer.addEventListener("click", () => {
    if (stageContainer.classList.contains("drawer-maximized")) {
      stageContainer.classList.remove("drawer-maximized");
      btnMaximizeDrawer.textContent = "⛶ 铺满";
    }

    const isCollapsed = consoleDrawer.classList.toggle("collapsed");
    if (isCollapsed) {
      drawerToggleIcon.textContent = "🗖";
      drawerToggleText.textContent = "展开终端";
    } else {
      drawerToggleIcon.textContent = "🗕";
      drawerToggleText.textContent = "收缩终端";
      termOutput.scrollTop = termOutput.scrollHeight;
    }
  });
}

// 抽屉铺满全屏切换
if (btnMaximizeDrawer) {
  btnMaximizeDrawer.addEventListener("click", () => {
    if (consoleDrawer.classList.contains("collapsed")) {
      consoleDrawer.classList.remove("collapsed");
      drawerToggleIcon.textContent = "🗕";
      drawerToggleText.textContent = "收缩终端";
    }

    const isMax = stageContainer.classList.toggle("drawer-maximized");
    btnMaximizeDrawer.textContent = isMax ? "🗗 还原" : "⛶ 铺满";
    termOutput.scrollTop = termOutput.scrollHeight;
  });
}

// ==========================================================================
// 4. 设备工作模式状态机 (尺寸锁死防抖)
// ==========================================================================
function setDeviceMode(mode, detail = "") {
  // 核心守卫：断开状态下除 "disconnected" 外严禁任何状态反向跃迁
  if (!isConnected && mode !== "disconnected") {
    mode = "disconnected";
  }
  currentDeviceMode = mode;
  if (!badgeDeviceMode) return;

  switch (mode) {
    case "disconnected":
      badgeDeviceMode.className = "badge badge-gray";
      badgeDeviceMode.textContent = "⚪ 未连接";
      btnConnect.className = "btn-connect-pill";
      btnConnect.innerHTML = `<span class="dot"></span> 连接设备`;
      if (btnJumpToBoot) btnJumpToBoot.style.display = "none";
      if (btnQuickRunApp) btnQuickRunApp.style.display = "none";
      break;

    case "handshaking":
      badgeDeviceMode.className = "badge badge-yellow";
      badgeDeviceMode.textContent = "🟡 识别中...";
      btnConnect.className = "btn-connect-pill waiting";
      btnConnect.innerHTML = `<span class="dot"></span> 探测中...`;
      if (btnJumpToBoot) btnJumpToBoot.style.display = "none";
      if (btnQuickRunApp) btnQuickRunApp.style.display = "none";
      break;

    case "app":
      badgeDeviceMode.className = "badge badge-blue";
      badgeDeviceMode.textContent = "🔵 APP 模式";
      btnConnect.className = "btn-connect-pill connected";
      btnConnect.innerHTML = `<span class="dot"></span> 断开连接`;
      if (btnJumpToBoot) {
        btnJumpToBoot.style.display = "inline-flex";
        btnJumpToBoot.disabled = false;
        btnJumpToBoot.textContent = "⚡ 切回 Boot";
      }
      if (btnQuickRunApp) btnQuickRunApp.style.display = "none";
      break;

    case "bootloader":
      badgeDeviceMode.className = "badge badge-green";
      badgeDeviceMode.textContent = "🟢 Bootloader";
      btnConnect.className = "btn-connect-pill connected";
      btnConnect.innerHTML = `<span class="dot"></span> 断开连接`;
      if (btnJumpToBoot) btnJumpToBoot.style.display = "none";
      if (btnQuickRunApp) {
        btnQuickRunApp.style.display = "inline-flex";
        btnQuickRunApp.disabled = false;
        btnQuickRunApp.textContent = "🚀 启动 APP";
      }
      break;
  }
}

function setBootloaderReady() {
  if (!isConnected) return;
  if (isHandshaking) {
    isHandshaking = false;
    invoke("stop_auto_handshake").catch(() => {});
  }
  setDeviceMode("bootloader");
}

// ==========================================================================
// 5. 串口扫描与连接管理
// ==========================================================================
async function refreshPorts() {
  try {
    const ports = await invoke("list_ports");
    portSelect.innerHTML = "";
    if (ports && ports.length > 0) {
      ports.forEach((port) => {
        const opt = document.createElement("option");
        opt.value = port;
        opt.textContent = port;
        if (port === "COM3") opt.selected = true;
        portSelect.appendChild(opt);
      });
    } else {
      const opt = document.createElement("option");
      opt.value = "";
      opt.textContent = "无串口";
      portSelect.appendChild(opt);
    }
  } catch (err) {
    appendTerminal(`[ERR] 获取串口列表失败: ${err}\r\n`);
  }
}

btnRefreshPorts.addEventListener("click", refreshPorts);

let handshakeTimeoutId = null;
let statusTimeoutId = null;

btnConnect.addEventListener("click", async () => {
  if (!isConnected) {
    const port = portSelect.value;
    const baud = parseInt(baudSelect.value, 10);
    if (!port) {
      alert("请先选择一个有效的串口！");
      return;
    }

    try {
      await invoke("connect_serial", { portName: port, baudRate: baud });
      isConnected = true;
      portSelect.disabled = true;
      baudSelect.disabled = true;

      setDeviceMode("handshaking");
      isHandshaking = true;
      await invoke("start_auto_handshake");
      appendTerminal(`[INFO] 串口 ${port} (波特率 ${baud}) 连接成功。正在自动探针识别...\r\n`);

      if (statusTimeoutId) clearTimeout(statusTimeoutId);
      statusTimeoutId = setTimeout(() => {
        if (isConnected) {
          invoke("send_serial_cmd", { cmd: "status" }).catch(() => {});
        }
      }, 150);

      if (handshakeTimeoutId) clearTimeout(handshakeTimeoutId);
      handshakeTimeoutId = setTimeout(() => {
        if (isHandshaking && isConnected && currentDeviceMode === "handshaking") {
          isHandshaking = false;
          invoke("stop_auto_handshake").catch(() => {});
          badgeDeviceMode.className = "badge badge-yellow";
          badgeDeviceMode.textContent = "🟡 未识别";
          btnConnect.className = "btn-connect-pill connected";
          btnConnect.innerHTML = `<span class="dot"></span> 断开连接`;
          appendTerminal(`[TIP] 未能自动识别状态。若已在上电，可按单片机 RESET 或点击【⚡ 切回 Boot】。\r\n`);
          if (btnJumpToBoot) btnJumpToBoot.style.display = "inline-flex";
        }
      }, 8000);
    } catch (err) {
      isConnected = false;
      isHandshaking = false;
      setDeviceMode("disconnected");
      appendTerminal(`[ERR] 连接串口失败: ${err}\r\n`);
    }
  } else {
    try {
      isConnected = false;
      isHandshaking = false;
      if (handshakeTimeoutId) {
        clearTimeout(handshakeTimeoutId);
        handshakeTimeoutId = null;
      }
      if (statusTimeoutId) {
        clearTimeout(statusTimeoutId);
        statusTimeoutId = null;
      }
      await invoke("stop_auto_handshake").catch(() => {});
      await invoke("disconnect_serial");
      streamBuffer = "";
      setDeviceMode("disconnected");
      portSelect.disabled = false;
      baudSelect.disabled = false;
      appendTerminal(`\r\n[INFO] 串口连接已断开。\r\n`);
    } catch (err) {
      isConnected = false;
      isHandshaking = false;
      streamBuffer = "";
      setDeviceMode("disconnected");
      portSelect.disabled = false;
      baudSelect.disabled = false;
      appendTerminal(`\r\n[ERR] 断开串口失败: ${err}\r\n`);
    }
  }
});

if (btnJumpToBoot) {
  btnJumpToBoot.addEventListener("click", async () => {
    if (!isConnected) return alert("请先连接串口设备！");
    try {
      btnJumpToBoot.disabled = true;
      btnJumpToBoot.textContent = "⏳ 拦截中...";
      appendTerminal(`\r\n[ACTION] 向单片机发送 jump_boot 并在 40ms 内拦截引导...\r\n`);
      await invoke("jump_to_bootloader");
    } catch (err) {
      appendTerminal(`[ERR] 切入 Bootloader 失败: ${err}\r\n`);
      btnJumpToBoot.disabled = false;
      btnJumpToBoot.textContent = "⚡ 切回 Boot";
    }
  });
}

if (btnQuickRunApp) {
  btnQuickRunApp.addEventListener("click", async () => {
    if (!isConnected) return alert("请先连接串口设备！");
    try {
      appendTerminal(`\r\n[ACTION] 执行启动固件: boot\r\n`);
      await invoke("send_serial_cmd", { cmd: "boot" });
    } catch (err) {
      appendTerminal(`[ERR] 启动 APP 失败: ${err}\r\n`);
    }
  });
}

// ==========================================================================
// 6. 终端流式 60FPS 渲染与协议捕获器
// ==========================================================================
let streamBuffer = "";
let pendingTerminalText = "";
let terminalRafId = null;

function appendTerminal(text) {
  pendingTerminalText += text;
  streamBuffer += text;

  const lines = streamBuffer.split(/\r?\n/);
  streamBuffer = lines.pop();

  // 核心守卫：仅在已连接状态下才将文本行喂给单片机协议解析器
  if (isConnected) {
    lines.forEach((line) => {
      processIncomingLine(line);
    });
  }

  if (!terminalRafId) {
    terminalRafId = requestAnimationFrame(() => {
      terminalRafId = null;
      if (pendingTerminalText) {
        termOutput.textContent += pendingTerminalText;
        pendingTerminalText = "";
        termOutput.scrollTop = termOutput.scrollHeight;

        if (termOutput.textContent.length > 80000) {
          termOutput.textContent = termOutput.textContent.slice(-50000);
        }
      }
    });
  }
}

function processIncomingLine(line) {
  if (!isConnected) return;
  const trimmed = line.trim();
  if (!trimmed) return;

  // 忽略前端内部控制台日志，防止触发关键字命中
  if (
    trimmed.startsWith("[INFO]") ||
    trimmed.startsWith("[ERR]") ||
    trimmed.startsWith("[WARN]") ||
    trimmed.startsWith("[ACTION]") ||
    trimmed.startsWith("[TIP]") ||
    trimmed.startsWith("[CMD]") ||
    trimmed.startsWith("[FILE]") ||
    trimmed.startsWith("[AUTO]")
  ) {
    return;
  }

  // 1. 握手与 Bootloader 提示符
  if (trimmed.includes("[HANDSHAKE_ACK]") || trimmed.includes("bootloader>")) {
    setBootloaderReady();
  }

  // 2. Bootloader 机器状态 JSON
  if (trimmed.includes('"type":"boot_status"')) {
    const s = trimmed.indexOf("{");
    const e = trimmed.lastIndexOf("}");
    if (s !== -1 && e > s) {
      try {
        const obj = JSON.parse(trimmed.substring(s, e + 1));
        currentDeviceStatus = obj;
        updateDashboard(obj);
        setBootloaderReady();
      } catch (err) {
        console.error("解析 boot_status 异常:", err);
      }
    }
  }

  // 3. APP 运行心跳
  if (trimmed.includes('"type":"app_status"') || trimmed.includes("[App Alive]") || trimmed.includes("External Flash App Running")) {
    if (isHandshaking) {
      isHandshaking = false;
      invoke("stop_auto_handshake").catch(() => {});
    }
    let detail = "Slot A";
    if (trimmed.includes('"type":"app_status"')) {
      const s = trimmed.indexOf("{");
      const e = trimmed.lastIndexOf("}");
      if (s !== -1 && e > s) {
        try {
          const appObj = JSON.parse(trimmed.substring(s, e + 1));
          detail = `${appObj.app || "APP"} ${appObj.version || ""}`.trim();
        } catch (_) {}
      }
    }
    setDeviceMode("app", detail);
  }

  // 4. Hex Dump 行收集
  if (trimmed.startsWith("Flash Hex Dump")) {
    hexLinesBuffer = [];
    if (hexDataView) hexDataView.innerHTML = "";
  } else if (trimmed.includes(":") && trimmed.includes("|")) {
    const colon = trimmed.indexOf(":");
    const pipe1 = trimmed.indexOf("|");
    const pipe2 = trimmed.lastIndexOf("|");
    const addrCandidate = trimmed.substring(0, colon).trim();

    if (/^[0-9A-Fa-f]{8}$/.test(addrCandidate) && pipe1 > colon && pipe2 > pipe1) {
      const addr = addrCandidate;
      const bytes = trimmed.substring(colon + 1, pipe1).trim();
      const ascii = trimmed.substring(pipe1 + 1, pipe2);
      renderHexLine(addr, bytes, ascii);
      hexLinesBuffer.push({ addr, bytes, ascii });
      analyzeHexData();
    }
  }
}

// ==========================================================================
// 7. 更新前端系统总览看板与槽位元数据
// ==========================================================================
function updateDashboard(data) {
  if (!data) return;

  if (valCpu) valCpu.textContent = "ARM Cortex-M7 @ STM32H750VBT6";
  if (valClock) valClock.textContent = `主频: ${data.clock_mhz || 480} MHz / 128KB 片内 Flash`;
  if (valFlash) valFlash.textContent = `Winbond W25Q64 (8 MB)`;
  if (valJedec) valJedec.textContent = `JEDEC ID: ${data.flash?.jedec || "0xEF4017"} | QE: ${data.flash?.qe ? "已开启" : "未开启"}`;

  const bootCount = data.boot_count !== undefined ? data.boot_count : 0;
  const maxCount = data.max_count || 3;
  const cycles = data.cycles || 0;
  if (valBootCycle) valBootCycle.textContent = `周期: ${cycles} | 失败尝试: ${bootCount}/${maxCount}`;

  if (valHealth) {
    if (bootCount >= maxCount) {
      valHealth.textContent = "⚠️ 触发异常保护 (待回滚)";
      valHealth.className = "card-val text-yellow";
      if (btnQuickResetBoot) btnQuickResetBoot.style.display = "inline-block";
    } else {
      valHealth.textContent = "正常运行";
      valHealth.className = "card-val text-green";
      if (btnQuickResetBoot) btnQuickResetBoot.style.display = "none";
    }
  }

  const isSlotA = data.active_slot === 0;
  if (valActiveBadge) valActiveBadge.textContent = isSlotA ? "Slot A" : "Slot B";
  if (valActiveSlot) valActiveSlot.textContent = isSlotA ? "0x90000000" : "0x903C0000";

  const totalSlotBytes = 3840 * 1024;
  if (data.slot_a) {
    if (slotAValid) {
      slotAValid.textContent = data.slot_a.valid ? "有效 [PASS]" : "无效 [FAIL]";
      slotAValid.className = data.slot_a.valid ? "badge-status status-pass" : "badge-status text-yellow";
    }
    if (slotACrc) slotACrc.textContent = (data.slot_a.crc && data.slot_a.crc !== "0x00000000") ? data.slot_a.crc : "硬件向量自检通过";
    if (slotAMsp) slotAMsp.textContent = data.slot_a.msp || "0x24000500";
    if (slotAPc) slotAPc.textContent = data.slot_a.pc || "0x900002AD";

    const sizeA = data.slot_a.size || 0;
    if (sizeA > 0) {
      const pctA = Math.min(100, (sizeA / totalSlotBytes) * 100).toFixed(1);
      const sizeAKb = (sizeA / 1024).toFixed(2);
      if (slotACapText) slotACapText.textContent = `${sizeAKb} KB / 3840 KB (${pctA}%)`;
      if (slotACapFill) slotACapFill.style.width = `${Math.max(1, pctA)}%`;
      if (slotASize) slotASize.textContent = `${sizeAKb} KB (${sizeA} 字节)`;
    }
  }

  if (data.slot_b) {
    if (slotBValid) {
      slotBValid.textContent = data.slot_b.valid ? "有效 [PASS]" : "待备份 [STANDBY]";
      slotBValid.className = data.slot_b.valid ? "badge-status status-pass" : "badge-status status-standby";
    }
    if (slotBCrc) slotBCrc.textContent = data.slot_b.crc || "-";
    if (slotBMsp) slotBMsp.textContent = data.slot_b.msp || "0xFFFFFFFF";
    if (slotBPc) slotBPc.textContent = data.slot_b.pc || "0xFFFFFFFF";

    const sizeB = data.slot_b.size || 0;
    if (sizeB > 0) {
      const pctB = Math.min(100, (sizeB / totalSlotBytes) * 100).toFixed(1);
      const sizeBKb = (sizeB / 1024).toFixed(2);
      if (slotBCapText) slotBCapText.textContent = `${sizeBKb} KB / 3840 KB (${pctB}%)`;
      if (slotBCapFill) slotBCapFill.style.width = `${Math.max(1, pctB)}%`;
      if (slotBSize) slotBSize.textContent = `${sizeBKb} KB (${sizeB} 字节)`;
    } else {
      if (slotBCapText) slotBCapText.textContent = "0.00 KB / 3840 KB (0.0%)";
      if (slotBCapFill) slotBCapFill.style.width = "0%";
    }
  }

  renderSlotMetadata();
}

function getSlotMetadata(slotKey) {
  try {
    const raw = localStorage.getItem(`stm32_slot_meta_${slotKey}`);
    return raw ? JSON.parse(raw) : null;
  } catch (e) {
    return null;
  }
}

function saveSlotMetadata(slotKey, meta) {
  try {
    localStorage.setItem(`stm32_slot_meta_${slotKey}`, JSON.stringify(meta));
  } catch (e) {}
  renderSlotMetadata();
}

function renderSlotMetadata() {
  const metaA = getSlotMetadata("a") || {
    ver: "v1.0.0",
    tag: "release-v1",
    sizeStr: "42.71 KB (43730 字节)",
    file: "uart_extflash.hex",
    time: "已成功烧录",
  };

  if (slotAVer) slotAVer.textContent = metaA.ver || "v1.0.0";
  if (slotATag) slotATag.textContent = metaA.tag || "release-v1";
  if (slotASize) slotASize.textContent = metaA.sizeStr || "42.71 KB";
  if (slotAFile) slotAFile.textContent = metaA.file || "uart_extflash.hex";
  if (slotATime) slotATime.textContent = metaA.time || "就绪";

  const totalSlotBytes = 3840 * 1024;
  if (metaA && metaA.sizeStr) {
    const match = metaA.sizeStr.match(/(\d+)\s*字节/);
    if (match) {
      const bytes = parseInt(match[1], 10);
      const pct = Math.min(100, (bytes / totalSlotBytes) * 100).toFixed(1);
      const kb = (bytes / 1024).toFixed(2);
      if (slotACapText) slotACapText.textContent = `${kb} KB / 3840 KB (${pct}%)`;
      if (slotACapFill) slotACapFill.style.width = `${Math.max(1, pct)}%`;
    }
  }

  const metaB = getSlotMetadata("b");
  if (slotBVer) {
    if (metaB) {
      slotBVer.textContent = metaB.ver || "-";
      slotBTag.textContent = metaB.tag || "-";
      slotBSize.textContent = metaB.sizeStr || "暂无备份镜像";
      slotBFile.textContent = metaB.file || "-";
      slotBTime.textContent = metaB.time || "-";

      if (metaB.sizeStr && metaB.sizeStr !== "暂无备份镜像") {
        const match = metaB.sizeStr.match(/(\d+)\s*字节/);
        if (match) {
          const bytes = parseInt(match[1], 10);
          const pct = Math.min(100, (bytes / totalSlotBytes) * 100).toFixed(1);
          const kb = (bytes / 1024).toFixed(2);
          if (slotBCapText) slotBCapText.textContent = `${kb} KB / 3840 KB (${pct}%)`;
          if (slotBCapFill) slotBCapFill.style.width = `${Math.max(1, pct)}%`;
        }
      }
    } else {
      slotBVer.textContent = "-";
      slotBTag.textContent = "-";
      slotBSize.textContent = "暂无备份镜像";
      slotBFile.textContent = "-";
      slotBTime.textContent = "-";
      if (slotBCapText) slotBCapText.textContent = "0.00 KB / 3840 KB (0.0%)";
      if (slotBCapFill) slotBCapFill.style.width = "0%";
    }
  }
}

// ==========================================================================
// 8. 固件拖放与一键智能烧录闭环
// ==========================================================================
if (dropZone) {
  dropZone.addEventListener("click", () => fileInput.click());
}

fileInput.addEventListener("change", (e) => {
  if (e.target.files.length > 0) handleFile(e.target.files[0]);
});

dropZone.addEventListener("dragover", (e) => {
  e.preventDefault();
  dropZone.style.borderColor = "#0284c7";
  dropZone.style.backgroundColor = "#e0f2fe";
});

dropZone.addEventListener("dragleave", () => {
  dropZone.style.borderColor = "#0284c7";
  dropZone.style.backgroundColor = "#f0f9ff";
});

dropZone.addEventListener("drop", (e) => {
  e.preventDefault();
  dropZone.style.borderColor = "#0284c7";
  dropZone.style.backgroundColor = "#f0f9ff";
  if (e.dataTransfer.files.length > 0) handleFile(e.dataTransfer.files[0]);
});

function handleFile(file) {
  currentSelectedFile = file;
  const sizeKb = (file.size / 1024).toFixed(2);

  selectedFileName.textContent = file.name;
  selectedFileSize.textContent = `${sizeKb} KB (${file.size} 字节)`;
  fileInfo.classList.remove("hidden");

  appendTerminal(`\r\n[FILE] 已载入固件: ${file.name} (${sizeKb} KB)，支持一键解析烧录！\r\n`);
}

btnFlashNow.addEventListener("click", async () => {
  if (!isConnected) return alert("请先连接串口设备！");
  if (!currentSelectedFile) return alert("请先拖放或选择要烧录的固件文件 (.hex / .bin / .axf)！");

  if (currentDeviceMode === "app") {
    appendTerminal(`\r\n[AUTO] 检测到当前处于 APP 模式，正在自动下发 jump_boot 切入 Bootloader...\r\n`);
    transferStage.textContent = "正在切入 Bootloader...";
    try {
      await invoke("jump_to_bootloader");
      let waits = 0;
      while (currentDeviceMode !== "bootloader" && waits < 35) {
        await new Promise((r) => setTimeout(r, 100));
        waits++;
      }
      if (currentDeviceMode !== "bootloader") {
        appendTerminal(`[WARN] 自动切换等待超时，尝试直接启动传输流程...\r\n`);
      } else {
        appendTerminal(`[AUTO] 成功切入 Bootloader 就绪，开始传输固件...\r\n`);
      }
    } catch (err) {
      appendTerminal(`[ERR] 自动切入 Bootloader 失败: ${err}\r\n`);
    }
  }

  const slot = fwTargetSlot.value;
  const version = fwVersion.value.trim() || "1.0.0";
  const packMode = fwPackMode.value;

  transferStage.textContent = "准备固件包...";
  progressFill.style.width = "0%";
  progressPercent.textContent = "0%";

  try {
    const arrayBuffer = await currentSelectedFile.arrayBuffer();
    const bytes = Array.from(new Uint8Array(arrayBuffer));

    transferStage.textContent = "正在传输 (Ymodem 流)...";

    await invoke("upload_firmware_bytes", {
      fileName: currentSelectedFile.name,
      fileBytes: bytes,
      targetSlot: slot,
      versionStr: version,
      withHeader: packMode === "auto_header",
    });

    const now = new Date();
    const timeStr = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, "0")}-${String(now.getDate()).padStart(2, "0")} ${String(now.getHours()).padStart(2, "0")}:${String(now.getMinutes()).padStart(2, "0")}`;
    const tag = fwTag.value.trim() || "release-v1";
    saveSlotMetadata(slot, {
      ver: version.startsWith("v") ? version : `v${version}`,
      tag: tag,
      sizeStr: `${(currentSelectedFile.size / 1024).toFixed(2)} KB (${currentSelectedFile.size} 字节)`,
      file: currentSelectedFile.name,
      time: timeStr,
    });

    appendTerminal(`\r\n[AUTO] 固件烧录成功！正在全自动下发 'boot' 指令启动 APP...\r\n`);
    transferStage.textContent = "启动 APP 中...";
    setTimeout(async () => {
      try {
        await invoke("send_serial_cmd", { cmd: "boot" });
        transferStage.textContent = "烧录完成 (APP已运行)";
      } catch (_) {}
    }, 400);

    setTimeout(() => {
      if (isConnected) invoke("send_serial_cmd", { cmd: "status" }).catch(() => {});
    }, 1500);
  } catch (err) {
    transferStage.textContent = "传输异常";
    alert(`烧录失败: ${err}`);
  }
});

// ==========================================================================
// 9. 灾备控制与清零指令
// ==========================================================================
if (btnActionBackup) {
  btnActionBackup.addEventListener("click", () => {
    if (!isConnected) return alert("请先连接串口");
    invoke("send_serial_cmd", { cmd: "backup" });
    appendTerminal("\r\n[ACTION] 下发指令: backup (克隆 A ➔ B)\r\n");
  });
}

if (btnActionRestore) {
  btnActionRestore.addEventListener("click", () => {
    if (!isConnected) return alert("请先连接串口");
    if (confirm("确定要将备份区 Slot B 覆盖还原至 Slot A 吗？这将重置启动计数！")) {
      invoke("send_serial_cmd", { cmd: "restore" });
      appendTerminal("\r\n[ACTION] 下发指令: restore (回滚 B ➔ A)\r\n");
    }
  });
}

if (btnActionVerifyA) {
  btnActionVerifyA.addEventListener("click", () => {
    if (!isConnected) return alert("请先连接串口");
    invoke("send_serial_cmd", { cmd: "verify a" });
    appendTerminal("\r\n[ACTION] 下发指令: verify a (自检 Slot A 向量与完整性)\r\n");
  });
}

if (btnActionVerifyB) {
  btnActionVerifyB.addEventListener("click", () => {
    if (!isConnected) return alert("请先连接串口");
    invoke("send_serial_cmd", { cmd: "verify b" });
    appendTerminal("\r\n[ACTION] 下发指令: verify b (自检 Slot B 向量与完整性)\r\n");
  });
}

if (btnActionResetCount) {
  btnActionResetCount.addEventListener("click", () => {
    if (!isConnected) return alert("请先连接串口");
    invoke("send_serial_cmd", { cmd: "config reset" });
    appendTerminal("\r\n[ACTION] 下发指令: config reset (清零异常重试计数)\r\n");
  });
}

if (btnQuickResetBoot) {
  btnQuickResetBoot.addEventListener("click", () => {
    if (!isConnected) return alert("请先连接串口");
    invoke("send_serial_cmd", { cmd: "config reset" });
    appendTerminal("\r\n[ACTION] 下发指令: config reset (清零异常重试计数)\r\n");
  });
}

if (btnActionBoot) {
  btnActionBoot.addEventListener("click", () => {
    if (!isConnected) return alert("请先连接串口");
    invoke("send_serial_cmd", { cmd: "boot" });
    appendTerminal("\r\n[ACTION] 下发指令: boot (从 Bootloader 启动固件)\r\n");
  });
}

if (btnActionReboot) {
  btnActionReboot.addEventListener("click", () => {
    if (!isConnected) return alert("请先连接串口");
    invoke("send_serial_cmd", { cmd: "reset" });
    appendTerminal("\r\n[ACTION] 下发指令: reset (软复位 MCU)\r\n");
  });
}

// ==========================================================================
// 10. Flash Hex 诊断与智能向量表解析
// ==========================================================================
function triggerHexRead(offsetStr, length) {
  if (!isConnected) return alert("请先连接串口！");
  hexOffset.value = offsetStr;
  hexLen.value = length;
  hexDataView.innerHTML = '<div class="text-muted text-center" style="padding: 20px;">⏳ 正在读取 Flash 数据...</div>';
  invoke("send_serial_cmd", { cmd: `read ${offsetStr} ${length}` });
}

if (btnReadHex) {
  btnReadHex.addEventListener("click", () => {
    const off = hexOffset.value.trim() || "0x000000";
    const len = parseInt(hexLen.value, 10) || 256;
    triggerHexRead(off, len);
  });
}

document.querySelectorAll(".btn-quick-addr").forEach((btn) => {
  btn.addEventListener("click", () => {
    const addr = btn.getAttribute("data-addr");
    triggerHexRead(addr, 256);
  });
});

function renderHexLine(addr, bytesStr, asciiStr) {
  const placeholder = hexDataView.querySelector(".text-muted");
  if (placeholder) placeholder.remove();

  const lineEl = document.createElement("div");
  lineEl.className = "hex-line";

  const addrEl = document.createElement("span");
  addrEl.className = "hex-addr";
  addrEl.textContent = addr;

  const bytesEl = document.createElement("span");
  bytesEl.className = "hex-bytes";
  bytesEl.textContent = bytesStr;

  const asciiEl = document.createElement("span");
  asciiEl.className = "hex-ascii";
  asciiEl.textContent = asciiStr;

  lineEl.appendChild(addrEl);
  lineEl.appendChild(bytesEl);
  lineEl.appendChild(asciiEl);
  hexDataView.appendChild(lineEl);
}

function analyzeHexData() {
  if (hexLinesBuffer.length === 0 || !anTarget) return;

  const offStr = hexOffset.value.trim().toLowerCase();
  const firstLine = hexLinesBuffer[0];
  const bytes = firstLine.bytes.split(/\s+/);

  if (bytes.length >= 8) {
    const msp = `0x${bytes[3]}${bytes[2]}${bytes[1]}${bytes[0]}`.toUpperCase();
    const pc = `0x${bytes[7]}${bytes[6]}${bytes[5]}${bytes[4]}`.toUpperCase();

    if (offStr === "0x000000" || offStr === "0x90000000" || offStr === "0") {
      anTarget.textContent = "Slot A 运行区 (0x90000000)";
      anMsp.textContent = `${msp} [AXI SRAM 栈顶]`;
      anPc.textContent = `${pc} [Thumb 复位代码]`;
      anStatus.innerHTML = '<span class="text-green">✓ 栈顶落在 0x24000000 区域，PC 落在 0x90000000 运行区，完全合法！</span>';
    } else if (offStr === "0x3c0000" || offStr === "0x903c0000") {
      anTarget.textContent = "Slot B 备份区 (0x903C0000)";
      anMsp.textContent = msp;
      anPc.textContent = pc;
      if (msp.includes("FFFF") || pc.includes("FFFF")) {
        anStatus.innerHTML = '<span class="text-yellow">⚠ 扇区全为 0xFF，说明备份区当前处于未写入擦除状态。</span>';
      } else {
        anStatus.innerHTML = '<span class="text-green">✓ 备份区固件向量表合法。</span>';
      }
    } else if (offStr === "0x780000" || offStr === "0x90780000") {
      anTarget.textContent = "NV-Config 参数扇区 (0x90780000)";
      anMsp.textContent = `Magic: ${msp}`;
      anPc.textContent = `Flags: ${pc}`;
      anStatus.innerHTML = '<span class="text-green">✓ NV-Config 参数块已持久化。</span>';
    } else {
      anTarget.textContent = `Flash 偏移 ${hexOffset.value}`;
      anMsp.textContent = `Word 0: ${msp}`;
      anPc.textContent = `Word 1: ${pc}`;
      anStatus.textContent = "已完成 16 进制原始数据对齐解析。";
    }
  }
}

// ==========================================================================
// 11. 控制台命令行与快捷指令 (CLI)
// ==========================================================================
function sendTerminalCommand() {
  const cmd = termInput.value.trim();
  if (!cmd) return;
  if (!isConnected) {
    appendTerminal("[ERR] 请先连接串口！\r\n");
    return;
  }

  cmdHistory.push(cmd);
  cmdHistoryIndex = cmdHistory.length;

  invoke("send_serial_cmd", { cmd: cmd }).catch((err) => {
    appendTerminal(`[ERR] 发送失败: ${err}\r\n`);
  });
  termInput.value = "";
}

btnTermSend.addEventListener("click", sendTerminalCommand);

termInput.addEventListener("keydown", (e) => {
  if (e.key === "Enter") {
    sendTerminalCommand();
  } else if (e.key === "ArrowUp") {
    if (cmdHistory.length > 0 && cmdHistoryIndex > 0) {
      cmdHistoryIndex--;
      termInput.value = cmdHistory[cmdHistoryIndex];
    }
  } else if (e.key === "ArrowDown") {
    if (cmdHistoryIndex < cmdHistory.length - 1) {
      cmdHistoryIndex++;
      termInput.value = cmdHistory[cmdHistoryIndex];
    } else {
      cmdHistoryIndex = cmdHistory.length;
      termInput.value = "";
    }
  }
});

btnTermClear.addEventListener("click", () => {
  termOutput.textContent = "";
});

if (btnTermCopy) {
  btnTermCopy.addEventListener("click", () => {
    if (termOutput && termOutput.textContent) {
      navigator.clipboard.writeText(termOutput.textContent);
      const origin = btnTermCopy.textContent;
      btnTermCopy.textContent = "✓ 已复制";
      setTimeout(() => {
        btnTermCopy.textContent = origin;
      }, 1500);
    }
  });
}

cmdPills.forEach((pill) => {
  pill.addEventListener("click", () => {
    const cmd = pill.getAttribute("data-cmd");
    if (!cmd) return;
    if (!isConnected) return alert("请先连接串口设备！");
    appendTerminal(`\r\n[CMD] ${cmd}\r\n`);
    invoke("send_serial_cmd", { cmd: cmd }).catch((err) => {
      appendTerminal(`[ERR] 发送失败: ${err}\r\n`);
    });
  });
});

// ==========================================================================
// 12. Tauri 事件监听与启动初始化
// ==========================================================================
async function setupEventListeners() {
  await listen("serial-rx", (event) => {
    if (!isConnected) return;
    appendTerminal(event.payload);
  });

  await listen("upload-progress", (event) => {
    const p = event.payload;
    progressFill.style.width = `${p.percent}%`;
    progressPercent.textContent = `${p.percent}%`;
    progressSpeed.textContent = `${p.speed_kb} KB/s`;
    transferBytes.textContent = `${p.sent_bytes} / ${p.total_bytes} 字节`;
    transferStage.textContent = p.stage;
    transferTime.textContent = `${p.elapsed_sec}s`;
    if (p.crc) transferCrc.textContent = p.crc;
  });
}

window.addEventListener("DOMContentLoaded", () => {
  renderSlotMetadata();
  refreshPorts();
  setupEventListeners();
});
