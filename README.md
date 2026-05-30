# Gungi raylib

這是一個 C99 + raylib 的軍儀棋第一版實作。規則核心在 `src/gungi_rules.c` / `src/gungi_rules.h`，raylib 畫面在 `src/main.c`，核心測試在 `tests/test_rules.c`。

## 最簡單玩法

一般玩家請到 GitHub Releases 下載 `gungi.exe`，直接雙擊執行即可：

https://github.com/LegendMMM/gungi/releases

## 從原始碼一鍵啟動

Windows 使用者下載或 clone 專案後，執行：

```bat
setup_windows.bat
```

這個腳本會自動下載可攜版 `w64devkit` 與 raylib 5.5 MinGW 套件到專案內的 `.deps`，接著建置、執行規則測試，最後啟動遊戲。

只建置與測試、不啟動視窗：

```bat
setup_windows.bat --no-run
```

macOS 使用者下載或 clone 專案後，執行：

```sh
./setup_macos.command
```

這個腳本會自動下載 raylib 5.5 原始碼到專案內的 `.deps`，接著建置、執行規則測試，最後啟動遊戲。

只建置與測試、不啟動視窗：

```sh
./setup_macos.command --no-run
```

## 手動建置

如果你已經安裝 raylib Windows package，也可以直接用既有建置腳本：

1. 確認 `gcc` 可在命令列使用；raylib installer 常見路徑是 `C:\raylib\w64devkit\bin`。
2. 預設 raylib 根目錄為 `C:\raylib\raylib`。如果安裝在其他位置，先設定：

```bat
set RAYLIB_PATH=C:\path\to\raylib
```

3. 建置：

```bat
build.bat
```

成功後會產生：

- `gungi.exe`
- `tests\test_rules.exe`

## 操作

- 左側是 Black hand，右側是 White hand，中間是 9x9 棋盤。
- 選 `New` 後點手駒，再點棋盤格放置手駒。
- 點目前回合玩家的棋子後，棋盤會提示可移動位置；點另一顆己方棋子會切換提示。
- 點空格會移動，點敵方棋子會吃子；按 `S` 後點可疊目標可執行 `Stack`。
- 快捷鍵：`N` New、`M` Move、`C` Capture、`S` Stack、`R` Restart、`Esc` / `X` 清除選取。
- `Restart` 重新開局，`Resign` 由目前回合玩家投降。

## 目前限制

- 使用固定預設布陣，不含完整輪流初始布陣階段。
- 已有 check 偵測與禁止自陷入 check，但尚未做完整 checkmate 搜尋。
- Captain turncoat、AI、線上對戰、存檔讀檔與美術素材不在第一版範圍。
- 棋子以 ASCII 代號顯示，不載入 CJK 字型。
