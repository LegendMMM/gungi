# Gungi AI 心路歷程與技術演進報告

日期：2026-06-17
定位：專題研究報告中的「心路歷程與技術演進」章節
主題：從 value-only 到 VP hybrid，再到 anti-draw 與 side-aware policy 的演進

## 1. 研究起點：不是只要更會贏，而是不要拖成和局

這一輪 Gungi AI 的研究，一開始並不是單純想讓模型「勝率更高」。真正的問題是：目前的 AI 雖然不容易輸，但常常把對局拖進長局、和局或 timeout。這種穩定性表面上看起來安全，實際上卻代表模型缺少結束對局的能力。

因此，我把目標重新定義成三件事：

```text
1. 降低 draw / timeout
2. 保有攻擊性，也就是不能只靠保守拖局避免輸棋
3. 維持 side balance，避免某一方特別強或特別弱
```

這個目標後來成為整個實驗演進的核心。每一版模型看似是在改訓練方式或 policy head，但更深層的變化其實是：我逐漸發現「模型要學什麼」比「模型跑多久」更重要。

## 2. V1：穩定但保守的第一個雙頭模型

第一個重要版本是 VP hybrid，也就是 value + policy 的雙頭模型。原本的 value-only student 只會判斷局面好壞，缺少 teacher 的選招偏好；因此我加入 policy head，希望它能在 hybrid search 裡改善 move ordering，讓搜尋更接近 teacher 的決策習慣。

V1 的正式 300 局結果是：

```text
V1 formal 300:
105 wins / 40 losses / 155 draws
timeouts = 133 / 300
black = 56-33-61
white = 49-7-94
avg ply = 606.98
```

這個結果有兩面性。一方面，V1 的 loss rate 很低，黑方表現也穩定；另一方面，draw 和 timeout 太高，`155/300` 局是和局，`133/300` 局是 timeout。這表示模型學到的不是「如何積極結束對局」，而是「如何在不容易輸的狀態下繼續拖」。

回頭看訓練資料，這個現象其實不意外。V1 的 teacher/self-play 樣本中，100 局全部跑到 1000-ply timeout。也就是說，policy head 雖然形式上學到了 teacher move ranking，但它看到的世界幾乎都是長局與未終局狀態。模型沒有足夠機會學到「什麼選擇會讓對局有效結束」。

這是第一個重要反思：

```text
如果訓練資料本身充滿 timeout，那麼加長訓練只會讓模型更穩定地學會拖局。
```

所以問題不只是訓練量，而是訓練訊號的方向。

## 3. V2A：第一次把「結束對局」當成學習訊號

V1 讓我意識到，光模仿 teacher 不足以解決和局問題。因此 V2A 的出發點，是讓 policy head 不只學「teacher 喜歡哪一步」，而是學「哪一步比較可能推動對局走向結果」。

這就是 resolution 訊號的由來。V2A 嘗試把 capture、check、material delta、repetition risk、rollout outcome 等資訊合成一個 resolution-aware target，讓模型偏好更有決斷性的走法。

V2A 的 600-ply 結果是：

```text
V2A 600-ply:
11 wins / 12 losses / 17 draws
timeouts = 11 / 40
black = 5-12-3
white = 6-0-14
```

和 V1 600-ply 對照：

```text
V1 600-ply:
12 wins / 5 losses / 23 draws
timeouts = 22 / 40
```

這是一個很有啟發性的失敗。V2A 幾乎把 timeout 減半，從 `22/40` 降到 `11/40`，說明 resolution 訊號確實有用；但 loss 從 `5/40` 增加到 `12/40`，尤其黑方變成 `5-12-3`，代表它也把模型推向了更容易輸的局面。

這讓我得到第二個反思：

```text
只教模型「快點結束」是不夠的。
模型必須分清楚「快速贏」和「快速輸」。
```

V2A 的價值不是它可以直接採用，而是它證明了 anti-draw 訊號確實能改變行為。它讓模型更有決斷性，但還沒有 loss-risk guardrail。

## 4. Policy Blend：以為是比例問題，結果發現是方向問題

V2A 太冒進，V1 太保守。很自然的下一步，是嘗試把兩者混合：保留 V1 的穩定性，同時注入 V2A 的 anti-draw 訊號。

我測試了多個 alpha：

```text
0.05, 0.10, 0.15, 0.25, 0.50, 0.75
```

混合方式是：

```text
policy = (1 - alpha) * V1_policy + alpha * V2A_policy
value  = V1_value
```

結果顯示，timeout 的確會下降，但黑方問題仍然存在。即使 alpha 只有 `0.05`，結果仍是：

```text
blend a005 600-ply:
15 wins / 12 losses / 13 draws
timeouts = 10 / 40
black = 4-12-4
white = 11-0-9
```

這個版本很有意思。白方 `11-0-9`，攻擊性明顯提升；但黑方 `4-12-4`，完全不能接受。更高 alpha 也有類似問題：timeout 降低，但黑方敗率偏高。

這讓我修正了原本的假設。問題不是「V2A 訊號太強，所以 alpha 調小就好」，而是這個訊號本身有 side-specific 副作用。它對白方可能是好的，因為白方原本就有嚴重 timeout farming；但它對黑方會破壞原本的穩定性。

第三個反思是：

```text
同一個 policy 訊號不一定適合兩邊。
有些改進不是全局有效，而是只對特定 side 有效。
```

## 5. Side-Aware：從全局 policy 轉向分邊策略

Policy blend 的結果讓我轉向 side-aware 設計。既然 V1 黑方穩定，而 a005 白方有攻擊性，那就不要強迫黑白方共用同一個 policy 行為。

side-aware 版本的設定是：

```text
value model        = V1
black policy model = V1
white policy model = anti-draw blend alpha 0.05
mode               = vp-side-hybrid
```

也就是：

```text
black to move: use V1 policy ordering
white to move: use anti-draw policy ordering
```

40 局小樣本檢查結果非常好：

```text
Side-aware 600-ply:
19 wins / 4 losses / 17 draws
timeouts = 15 / 40
black = 8-4-8
white = 11-0-9
```

對照 V1 同 seed：

```text
V1 same-seed 600-ply:
11 wins / 4 losses / 25 draws
timeouts = 23 / 40
black = 8-4-8
white = 3-0-17
```

1000-ply 40 局檢查也很好：

```text
Side-aware 1000-ply check:
22 wins / 5 losses / 13 draws
timeouts = 8 / 40
black = 9-4-7
white = 13-1-6
```

這一刻看起來像是找到了正確方向。黑方保持穩定，白方變得更有攻擊性，timeout 明顯下降。這也證明 side-aware 不是單純的 workaround，而是對實驗資料做出的合理回應。

不過，這裡也埋下了下一個陷阱：小樣本檢查很漂亮，不代表正式評估一定通過。

## 6. 正式 300 局後的修正：小樣本成功不等於正式通過

為了確認 side-aware 不是 seed 運氣或小樣本假象，我跑了三個 seed、共 300 局的正式評估。

正式結果是：

```text
Side-aware formal 300:
102 wins / 47 losses / 151 draws
timeouts = 115 / 300
black = 56-33-61
white = 46-14-90
avg ply = 543.43
```

對照 V1 formal 300：

```text
V1 formal 300:
105 wins / 40 losses / 155 draws
timeouts = 133 / 300
black = 56-33-61
white = 49-7-94
avg ply = 606.98
```

正式評估給了更冷靜的答案。side-aware 確實有改善 timeout：

```text
timeouts: 133 -> 115
avg ply: 606.98 -> 543.43
```

黑方也完全保住：

```text
V1 black         = 56-33-61
Side-aware black = 56-33-61
```

但問題也很明顯：

```text
wins:   105 -> 102
losses: 40  -> 47
draws:  155 -> 151
white:  49-7-94 -> 46-14-90
```

也就是說，side-aware 把一部分 timeout 轉成了結果，但那些結果不全是勝利。有些白方 timeout 被轉成敗局，導致白方 loss 從 `7` 增加到 `14`。因此正式結論不能寫成「side-aware 成功取代 V1」，而應該寫成：

```text
side-aware anti-draw 是有效方向，但 a005 white policy 還太冒進。
它降低 timeout，卻把部分白方 timeout 轉成敗局，正式採用未通過。
```

這是第四個反思：

```text
降低 timeout 不是終點。
真正的目標是降低 timeout，同時控制 loss-risk，並把更多長局轉成 win。
```

## 7. 最終反思：模型進化其實是問題定義的進化

回顧這一整段實驗，模型的演進看起來是從 V1 到 V2A，再到 blend 和 side-aware；但真正的進化，其實是問題定義的進化。

一開始，我以為問題是：

```text
模型缺少 policy，所以要加 policy head。
```

V1 後我發現，問題其實是：

```text
policy 學到的是 timeout teacher，不是終局決斷。
```

V2A 後我又發現：

```text
讓模型更快結束對局有效，但如果沒有風險控制，就會快速輸。
```

policy blend 後，我進一步發現：

```text
anti-draw 訊號不是全局中性的，它對黑白方有不同副作用。
```

side-aware formal 300 後，最後的修正是：

```text
分邊策略是對的，但白方 anti-draw 還需要 loss-risk guardrail。
```

所以這一輪最重要的成果，不是找到一個可以直接上線的模型，而是把下一版的問題定義得更準確。從「多訓練一點」到「修正訓練訊號」，從「單一 policy」到「side-aware policy」，再從「降低 timeout」到「降低 timeout 且控制 loss-risk」，這才是這次研究真正的進展。

## 8. 下一版目標：side-aware + loss-risk guardrail

下一版不應該回到單一 policy head，也不應該直接採用 a005 white policy。比較合理的方向是保留 side-aware 架構，但把白方 anti-draw 訊號拆得更細。

建議下一版 target 拆成三個部分：

```text
attack_score
timeout_risk
loss_risk
```

白方排序分數不應只追求決斷性，而應該加入風險閥：

```text
score = attack_score - timeout_risk - 2.0 * loss_risk
```

下一版 acceptance criteria 也應該直接用正式 300 局，而不是只看 40 局小樣本：

```text
wins >= 105
losses <= 40
draws < 151
timeouts < 115
white losses <= 8
black result >= 56-33-61
```

這樣定義後，下一版才不會只是在「少一點 timeout」和「多一點敗局」之間交換，而是真正往更有攻擊性、更能結束對局、也更穩定的 AI 前進。

## 9. 本輪總結

本輪研究沒有得到可以直接取代 V1 的正式模型，但它完成了更重要的事：找出和局問題的結構。

V1 告訴我，穩定不等於會贏；V2A 告訴我，決斷不等於正確；policy blend 告訴我，比例不是萬能；side-aware 告訴我，黑白方需要不同策略；formal 300 告訴我，小樣本成功必須接受正式驗證。

最後的結論是：

```text
Gungi AI 的下一步，不是單純加長訓練時間，
而是建立能同時理解 attack、timeout risk、loss risk 的 side-aware policy。
```

這就是本輪從 VP hybrid 到 anti-draw side-aware 的真正心路歷程。
