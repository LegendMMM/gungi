# Gungi raylib

這是一個 C99 + raylib 的軍儀棋第一版實作。規則核心在 `src/gungi_rules.c` / `src/gungi_rules.h`，raylib 畫面在 `src/main.c`，核心測試在 `tests/test_rules.c`。

## 安裝需求

1. 安裝 raylib Windows package。
2. 確認 `gcc` 可在命令列使用；raylib installer 常見路徑是 `C:\raylib\w64devkit\bin`。
3. 預設 raylib 根目錄為 `C:\raylib\raylib`。如果安裝在其他位置，先設定：

```bat
set RAYLIB_PATH=C:\path\to\raylib
```

## 建置

```bat
build.bat
```

成功後會產生：

- `gungi.exe`
- `tests\test_rules.exe`

如果本機沒有 gcc 或 raylib，腳本會停止並顯示缺少的項目。

## 執行

```bat
gungi.exe
tests\test_rules.exe
```

## 操作

- 左側是 Black hand，右側是 White hand，中間是 9x9 棋盤。
- 選 `New` 後點手駒，再點棋盤格放置手駒。
- 選棋盤上的棋子後，再點目標格執行 `Move`、`Capture` 或 `Stack`。
- 快捷鍵：`N` New、`M` Move、`C` Capture、`S` Stack、`R` Restart、`Esc` 清除選取。
- `Restart` 重新開局，`Resign` 由目前回合玩家投降。

## 目前限制

- 使用固定預設布陣，不含完整輪流初始布陣階段。
- 已有 check 偵測與禁止自陷入 check，但尚未做完整 checkmate 搜尋。
- Captain turncoat、AI、線上對戰、存檔讀檔與美術素材不在第一版範圍。
- 棋子以 ASCII 代號顯示，不載入 CJK 字型。
