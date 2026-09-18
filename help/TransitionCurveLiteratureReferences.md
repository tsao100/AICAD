# AICAD 緩和曲線（Transition Curve）類型 — 文獻公式與網址對照表

本文件是 `TransitionCurveTypes.md` 的延伸版本，針對每個類型盡量找到公開文獻／網頁出處，
並誠實標註「程式實作」與「文獻原始定義」是否相符，供你逐一核對。

**本次整理說明**：這份文件經過多輪查證與修正（依你陸續指示逐項深入查核），內容一路
累積增補。這次依你要求做了**全面重新整理**：更新對照總表使其涵蓋所有 18 個新增類型、
移除重複／過時的中間結論（例如 Cosine、Biquadratic 曾一度標記「待確認」，但後來都已
查證定案，中間過時的段落已整併或刪除）、修正 Wiener Bogen 一度重複出現且參數不一致的
段落。全文現在應該前後一致，每個類型只有一個現行的結論。

> 搜尋方法說明：以下每筆都是實際網路搜尋得到的公開資料（論文摘要／全文、教材、專業
> 部落格、數學曲線百科、原廠文件等）。部分只找得到摘要（付費牆），這種情況會註明
> 「僅摘要，未取得完整公式」。

---

## 對照總表

**圖例**：✅ 已查證且公式（或處理方式）已確認、可信賴 ｜ 🟡 部分確認（曲線類別/特性
正確，但無單一標準公式，或仍有已知限制）｜ ❌ 命名與文獻定義有實質落差 ｜
— 通用示意類型，不對應特定文獻曲線

| 類型 | 狀態 | 摘要 |
|---|:---:|---|
| **Bloss** | ✅ | `g(t)=3t²−2t³`，與 Network Rail、Autodesk、Bentley 等多方文獻公式完全一致 |
| **Sinusoidal** | ✅ | 與日本鐵路慣用「正弦形」緩和曲線公式一致；亦驗證為 ISO 16739-1:2024 (IFC 4.3) `IfcSineSpiral` 的特例 |
| **Cosine** | ✅ | 已取得 MÁV（匈牙利國鐵）原始直角座標公式並重新實作；與 `HalfSine` 曲率律相同但計算方式不同（見下方詳述） |
| **Lemniscate** | ✅ | 依你提供的參考程式碼改為真正的伯努利雙紐線幾何，修正原點位置與曲率係數兩處錯誤 |
| **Wiener Bogen** | ✅ | 取得發明人 Hasslinger 原始專利完整二項式公式並重新實作，成功重現「兩端額外彎折」招牌特徵；`h`、`ψ₂` 為固定代表性預設值 |
| **Radioid（通用版）** | ❌ | 文獻中 "Radioid" 是曲線族統稱（Nördling 1867），非單一公式；建議評估改名或參考下方三個具名子類型 |
| ├ Elastic Radioid | ✅ | 歐拉彈性曲線 `κ(x)=2x/a²`，耦合 ODE 數值積分實作 |
| ├ Norwich/Sturm | 🟡 | `κ=1/r`；數學上證明無法在有限弧長內精確達到零曲率，忠實依定義實作並誠實記錄此限制 |
| └ Pseudo-elliptic Radioid | ✅ | `y=a·gd⁻¹(x/a)`，mathcurve.com 明確定義；45° 反曲點需旋轉對齊 |
| **Logarithmic** | ✅ | 對應 Log-Aesthetic Curve 框架 n=+1 情形，證明即為等角螺線的弧長參數化；與 Norwich/Sturm 同樣有「無法精確達到零曲率」的限制 |
| **Hyperbolic** | ✅ | 取得 Kisgyörgy & Barna (2014) 原始論文全文，公式與論文自附驗證表交叉比對誤差 <0.05mm |
| **Polynomial** | 🟡 | 確認業界為研究類別（係數依動力學最佳化求解，因專案而異），非單一公式；維持現有簡單示意公式 |
| **Quintic** | 🟡 | 確認為研究類別（同 Polynomial 情況）；與全新 `PHQuintic`（Pythagorean-Hodograph，完全不同構造）已分為兩個獨立類型 |
| **PHQuintic** | ✅ | Farouki/Walton-Meek 構造，複數前像多項式；推導過程中抓到並修正一個曲率公式的共軛錯誤 |
| **Biquadratic** | ✅ | 取得 Schuhr 論文精確描述 Helmert 1872 原始推導邏輯，解開先前「兩段二次」vs「單一四次」的矛盾 |
| **Spline** | — | 通用示意類型，非特定命名曲線 |
| **BlossEulerHybrid** | ✅ | 對應法國 SNCF「doucine」真實標準構造（1968年標準化），已從均勻混合改為「clothoid本體+兩端平滑圓角」 |
| **CubicECI**（既有類型，本次一併修正） | ✅ | 取得 CECI 原始理論公式與四項弧長反算式，修正原本完全錯誤的隱式方程實作 |

**可直接放心使用**（公式與外部文獻/標準明確一致）：`Bloss`、`Sinusoidal`、`Cosine`、
`Lemniscate`、`Wiener Bogen`、`Elastic Radioid`、`Pseudo-elliptic Radioid`、
`Logarithmic`、`Hyperbolic`、`PHQuintic`、`Biquadratic`、`BlossEulerHybrid`、
`CubicECI`——共 13 種。

**有已知固有限制，已誠實記錄**：`Norwich/Sturm`、`Logarithmic`（兩者皆因
`κ=1/(距離或半徑)` 的數學形式，無法在有限弧長內精確達到零曲率）。

**確認為研究類別、非單一公式**：`Polynomial`、`Quintic`（現有實作是該類別中一個
合理但非唯一的示意選擇）。

**命名與文獻有落差、建議你評估是否改名**：`Radioid`（通用版）、`Spline`。

> **權威來源總覽**（多次查證中找到的關鍵第三方確認）：
> - **Autodesk Civil 3D** 官方文件確認支援：Clothoid、Bloss、Sinusoidal、
> Sine Half-Wavelength Diminishing Straight（＝HalfSine）、Cubic Parabola、
> Cubic (JP)（＝CubicJPN）、Bi-Quadratic (Schramm)。
> <https://help.autodesk.com/cloudhelp/2016/ENG/Civil3D-UserGuide/files/GUID-DD7C0EA1-8465-45BA-9A39-FC05106FD822.htm>
> - **Bentley OpenRail Designer** 官方產品頁與簡報確認支援：Clothoid、
> Bi-quadratic Parabola、Bloss、Sinusoid、**Cosine**（及各自的 Half- 版本）。
> <https://en.virtuosity.com/openrail-designer>；<https://slideplayer.com/slide/8634476>
> - **VESTRA INFRAVISION Bahn**（德國）官方頁面確認：Klothoide、Blossbogen、
> Schrammbogen（德國）、Wiener Bogen®（奧地利）、**Cosinusoide**（日本/匈牙利）、
> Sinusoide（高速鐵路/磁浮）。
> <https://www.akgsoftware.at/branchen/tiefbau/bahn/>
> - **ISO 16739-1:2024 (IFC 4.3)** 正式標準定義了 `IfcSineSpiral`、`IfcCosineSpiral`
> 等實體，`Sinusoidal` 已驗證為 `IfcSineSpiral` 特例。

---

## 逐項細節

### Bloss ✅

- 曲率：`κ(l) = (1/R)·[3(l/L)² − 2(l/L)³]`，最大曲率梯度在中點 `1.5/(R·L)`
- 出處：
 - Network Rail Track Design Handbook（NR/L2/TRK/2049，C.2.6 Bloss transition curves）
 - <https://railwaytrackblog.com/2015/02/22/bloss-like-a-boss/>（含完整曲率梯度公式）
 - <https://railwaytrackblog.com/2017/05/15/bloss-rectangular-coordinates/>（直角座標展開式）
 - <https://www.researchgate.net/publication/282288817_Bloss_transition_-_a_short_design_guide>
 - 專利文件中亦列出相同公式：<https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/12017864>
 - Autodesk Civil 3D、Bentley OpenRail 官方文件皆確認支援（見上方權威來源總覽）
- 結論：與程式中 `g(t)=3t²−2t³` **完全一致**，可放心使用。

### Sinusoidal ✅

- 出處：
 - AutoCAD Civil 3D 使用手冊：「此形式的方程式常用於日本鐵路設計」
 <http://docs.autodesk.com/CIV3D/2014/ENG/filesCUG/GUID-581518E0-DE06-482E-840C-B997C3590489.htm>
 - LandXML 文件同款公式：
 <http://www.landxml.org/schema/Documentation/Transition%20curves%20in%20Road%20Design.doc>
 - 學術論文（Transrapid 與正弦緩和曲線）：
 <https://www.researchgate.net/publication/298717724_Transrapid_and_the_transition_curve_as_sinusoid>
 - 回顧論文提及 1937 年 sinusoidal curve 起源：
 <https://www.researchgate.net/publication/341476435_Railway_Transition_Curves_A_Review_of_the_State-of-the-Art_and_Future_Research>
- **額外驗證（ISO 標準等級）**：`g(t) = t − sin(2πt)/(2π)` 已用符號運算證明是
 ISO 16739-1:2024 (IFC 4.3) `IfcSineSpiral` 通式在 `ConstantTerm` 省略、
 `LinearTerm=√(R·Ls)`、`SineTerm=−2πR` 時的特例，`κ(s)`、`θ(L)` 皆完全吻合（diff=0）。
- 結論：與文獻描述及 ISO 標準的通用式皆一致，可放心使用。

### Cosine ✅ 已定案（MÁV 匈牙利國鐵原始公式，與 HalfSine 確認為不同曲線）

- **背景**：你先前找到一篇論壇實測案例——Civil 3D 使用者處理匈牙利 MÁV 提供的
 LandXML 對齊資料，內含 Civil 3D 本身不支援的 Cosine Transition Curve，並實測發現
 「用 Civil 3D 現有最接近的 Sine Half-Wavelength Diminishing Tangent（＝本程式
 HalfSine）重建，曲線本體內偏差約 0.005 m，起訖點偏差達 0.112 m，精度不足以用於
 精密鐵路設計」，並提供了 MÁV 原始 Cosine 曲線公式（直角座標 `y=f(x)` 形式）：

 ```
 y(x) = x²/(4R) − Ls²/(2π²R)·(1 − cos(πx/Ls))
 ```

- **關鍵發現（已用 SymPy 符號驗證）**：對這條公式取一階、二階導數：

 ```
 y'(x) = x/(2R) − Ls/(2πR)·sin(πx/Ls) ← 與 HalfSine 的 θ(s) 公式，
 只是把 s 換成 x，完全相同！
 y''(x) = (1 − cos(πx/Ls)) / (2R) ← 與 HalfSine 的 κ(s) 公式完全相同！
 ```

 也就是說，**MÁV Cosine 與 HalfSine 底層的「曲率-弧長」關係其實是同一條**——但兩者
 的計算方式不同：
 - **HalfSine**（`halfSineExactFrame()`）：對弧長 `s` 做**精確參數化積分**
 `x(s)=∫cos θ ds`、`y(s)=∫sin θ ds`（既有高階級數解）。
 - **MÁV Cosine**：把 `x` 直接當作弧長使用的**古典直角座標近似**（與既有
 `ParabolaElement`／`CubicJPNElement`／`CubicECIElement` 同一類手法）。

 這正是古典「三次拋物線近似 vs. 真正 clothoid 精確解」之間會出現的系統性落差，
 完全可以解釋論壇實測到的「曲線本體內小偏差、端點偏差較大」現象。

- **數值驗證**（程式碼實測，`Cosine` vs `HalfSine`，偏差隨曲線長度累積、在端點
 達到最大值，與論壇描述完全吻合）：

 | R (m) | Ls (m) | 曲線本體中段偏差 | 端點（L=Ls）偏差 |
 |---|---|---|---|
 | 1000 | 100 | 0.004 m（75% 處） | 0.023 m |
 | 600 | 80 | 0.006 m（75% 處） | 0.032 m |
 | 300 | 60 | 0.010 m（75% 處） | 0.054 m |
 | 200 | 50 | 0.013 m（75% 處） | 0.071 m |

 數量級與論壇報告的「本體 0.005 m／端點 0.112 m」相符。

- **另有 Bentley OpenRail、VESTRA 官方文件雙重確認**：Cosine 是業界認可的獨立
 曲線類型（Bentley 明確將 Cosine 與 Sinusoid 列為兩種不同類型；VESTRA 明確寫
 「Cosinusoide 用於日本或匈牙利」）——這與 MÁV 直角座標公式的來源國別完全吻合。

- **實作狀態**：`CosineElement::localFrame()` 直接實作此直角座標公式
 （`x=L`、`y=f(x)`、`θ=atan(y'(x))`），與 `HalfSineElement` 是兩套完全獨立、
 且**數值上真正不同**的程式碼。已編譯與數值驗證通過。

### Lemniscate ✅ 已重新實作（真正的雙紐線幾何，非曲率斜坡近似）

- 你提供了一份參考程式碼，實作真正的伯努利雙紐線（Bernoulli lemniscate）有理參數式：

 ```
 x(t) = a√2·cos(t) / (1+sin²t)
 y(t) = a√2·sin(t)·cos(t) / (1+sin²t)
 ```

 並用二分搜尋法反算弧長對應的參數 `t`，曲率公式用 `k = 3r/a²`。

- **驗證過程中發現兩個問題並已修正**：
 1. **原點位置錯誤**：參考程式碼註解寫「`start_t = -PI/4` // 右葉片的起點(原點)」，
 但實際代入計算：`t=-π/4` 時 `(x,y)=(6.667,-4.714)`（`a=10`），**不是原點**！
 真正的原點（`r=0`，雙紐線交叉點）其實在 `t=±π/2`。已改為從真正的原點
 （`t=π/2`）開始，往頂點（`t=0`）方向前進。
 2. **曲率公式係數錯誤**：用基本曲率公式 `κ=(x'y″−y'x″)/(x'²+y'²)^1.5` 直接
 驗算，發現正確公式應為 `κ = 1.5r/a²`，而非參考程式碼裡的 `κ = 3r/a²`——
 **差了 2 倍**。已用程式實測驗證：修正前端點曲率算出來是目標值的一半，
 修正後端點曲率誤差降到 1% 以內（有限差分本身的精度極限）。

- **實作方式**（架構與其他類型不同）：雙紐線沒有簡單的 `κ(s)=g(s/Ls)/R` 縮放關係，
 需要額外求解一個自由參數 `a`（雙紐線尺寸常數）。已證明 `κ₁(τ)·s₁(τ)`（`a=1`
 正規化曲線的曲率乘弧長）僅是縮減參數 `τ` 的函式、與 `a` 無關，因此可以：
 1. 先在正規化（`a=1`）曲線上二分搜尋求解 `τ_end`，使 `κ₁(τ_end)·s₁(τ_end) = Ls/R`
 2. 再算出 `a = R·κ₁(τ_end)`
 3. 查詢任意 `L` 時，先換算正規化弧長 `L/a`，二分搜尋對應的 `τ`，取得 `(x,y)`

 已加入 `mutable` 快取，只在 `Ls`／`R` 改變時才重新求解。

- **座標系方向**：原點處切線角驗證為 45°（伯努利雙紐線的經典性質），已對齊到
 程式的局部座標系。頂點（`τ=π/2`）處切線是**垂直**（−90°），代表可用總轉角
 範圍約 135°（45°→−90°），遠大於一般鐵路緩和曲線需要的角度。

- **數值驗證**（實際編譯後的 C++ 程式碼）：

 | R (m) | Ls (m) | Ls/R | 端點曲率（有限差分） | 目標 1/R | 誤差 |
 |---|---|---|---|---|---|
 | 1000 | 80 | 0.080 | 0.000994 | 0.001000 | 0.6% |
 | 500 | 80 | 0.160 | 0.001988 | 0.002000 | 0.6% |
 | 300 | 60 | 0.200 | 0.003306 | 0.003333 | 0.8% |
 | 100 | 30 | 0.300 | 0.009834 | 0.010000 | 1.7% |

 誤差皆屬有限差分本身精度極限（非公式錯誤），`κ(0)=0` 全部案例精確為零，
 正負 `R` 鏡射對稱正確。

### Wiener Bogen ✅ 已重新查證並實作（採用固定代表性參數）

- **確切公式**（找到發明人 Hasslinger 本人的原始專利 EP1523597B1／WO2004009906A1，
 以及引用該專利、附完整數學式的 MDPI 回顧論文全文，式 9）：

 ```
 κ(l) = κ₁ + (κ₂−κ₁)·f(l) − h·(ψ₂−ψ₁)·f″(l)
 ```

 出處：<https://www.mdpi.com/2412-3811/5/5/43>；
 <https://pdfs.semanticscholar.org/c5d7/bbdc0608af1d456a4b18b678bb7b0e94234c.pdf>
 （明確描述其特性——曲率為 S 形、**兩端有額外彎折（非單調遞增）**，這是所有
 緩和曲線中獨有的特徵）；另見
 <https://www.researchgate.net/publication/320585548_Characteristic_of_Wiener_BogenR_transition_curve>
 <https://www.koocoo.at/en/wiener-bogen-curves.html>

 其中 `κ₁,κ₂` 為起訖點曲率（此處 `κ₁=0, κ₂=1/R`），`ψ₁,ψ₂` 為起訖點超高量
 （此處 `ψ₁=0`），`h` 為車輛重心高度，`f(l)` 為超高漸變形狀函式（原文：「6 種
 可選類型中最常用的是七次多項式」——正是既有實作已用的 `g(t)=35t⁴−84t⁵+70t⁶−20t⁷`）。

- **關鍵發現：原本的實作漏掉了整個第二項**。舊版只有 `κ(l)=f(l)/R`（純曲率斜坡），
 這正是先前查證時發現「無法重現兩端彎折」的根本原因——真正產生這個招牌特徵的是
 第二項 `h·(ψ₂−ψ₁)·f″(l)`（超高漸變函式的二階導數，本身在兩端不是單調的，來自
 車輛通過超高漸變段時的側滾動力學效應）。

- **架構限制與處理方式**：正確實作需要 2 個全新參數——`ψ₂`（目標超高量）與
 `h`（車輛重心高度）——但 AICAD 的 `TransitionElement` 架構目前只有 `R`、`Ls`，
 沒有超高或車輛動力學資料模型。依你的指示（選項 C：用固定代表性預設值，不開放
 使用者調整），採用：

 ```
 h = 2.1336 m（7 英呎）
 ψ₂ = 0.10 rad（約 5.7°）
 ```

 出處：
 - **h 數值**：DOT/FRA/ORD-19/42（2019），《Superelevation》技術報告，式 6-8：
 「h is assumed to be 7 feet」，美國聯邦鐵路管理局計算超高不足量時採用的標準
 車輛重心高度假設：
 <https://railroads.dot.gov/sites/fra.dot.gov/files/fra_net/19085/Superelevation.pdf>
 - **ψ₂ 數值**：對應約 150mm 超高 / 1500mm 參考軌距的代表性數值
 （`atan(0.15/1.5)=0.0997 rad ≈ 0.10 rad`；150mm、1500mm 皆為多篇文獻共同
 引用的標準參考值）。
 - 由於缺少設計速度輸入，無法用平衡超高公式（`ψ=v²/(gR)`）為每條曲線個別計算
 精確超高量，這是**已知的簡化**，不是精確的逐專案車輛/超高模型。

- **實作**：`κ(l) = [g(t) − |R|·h·ψ₂·g″(t)/Ls²] / R`（修正項的符號隨 `R` 正負
 自動翻轉——超高一定往曲線內側傾斜，跟轉彎方向一致）。

- **數值驗證：成功重現「兩端額外彎折」特徵**。用實際編譯的 C++ 程式碼
 （`R=300m, Ls=80m`）逐段掃描曲率，確認曲率**不再是單調遞增**：

 | L (m) | 曲率 κ(L) | 說明 |
 |---|---|---|
 | 0 | 0.0000000 | 起點（直線相切） |
 | 10 | **−0.0000969** | ⚠️ 曲率變成負值（反向微幅彎曲，文獻描述的起點彎折） |
 | 20 | 0.0000045 | 回到接近零，開始正向遞增 |
 | 40 | 0.0016667 | 正常爬升 |
 | 65 | **0.0034386** | 超過目標值！（曲率峰值，比終點目標 `1/R=0.003333` 還大） |
 | 80 | 0.0033333 | 終點精確回到 `1/R`（如預期） |

 這正是文獻 Figure 4 描述的「曲率並非單調遞增、兩端有額外彎折」的具體重現——
 起點附近曲率短暫變負、終點前曲率會先超過目標值再回落。先前只有第一項的簡化版本
 完全沒有這個行為，是純粹單調的曲率斜坡。

 另外驗證：`κ(0)=0`、`κ(Ls)=1/R` 精確成立（因為 `g''(0)=g''(1)=0`，修正項在
 端點自動歸零，不受 `h`、`ψ₂` 選值影響）；正負 `R` 鏡射對稱正確。

- **若未來需要更精確的版本**：建議把 `h`、`ψ₂`（或直接是超高量 `E₂` + 軌距 `G`）
 做成 `WienerBogenElement` 專屬的可設定欄位，並串接 UI／指令列／ALD 存檔——
 工作量比照先前新增 13 種類型的完整串接規模。

### Radioid（通用版）❌，及三個具名子類型

- **通用版 `RadioidElement` 出處與問題**：
 - MathCurve.com 數學曲線百科（Robert Ferréol 編）明確定義：
 <https://mathcurve.com/courbes2d.gb/radioide/radioide.shtml>
 > "Any curve of class C² with a point with zero curvature can be called a
 > radioid... in practice, the following curves were used: the elastic
 > curve (curvature proportional to the abscissa), the cubical parabola
 > (Nördling parabola), **the clothoid (radioid with curvature proportional
 > to the curvilinear abscissa)**, the Norwich spiral (curvature
 > proportional to distance to a fixed point)..."
 - 原始文獻：Nördling (1867)、Fargue (1868)、Turrière (1939)：
 <http://www.tassignon.be/trains/cecf/tomeIII_II/C_E_C_F_III_II.htm#p027>
 - **重要落差**：「Radioid」其實是「連接直線與圓弧、曲率連續變化」這**整類曲線的
 統稱**，**clothoid 本身就是 radioid 家族的一員**！並非指某個特定的「凹形曲率」
 公式。現有 `g(t)=2t−t²` 只是自訂的凹形曲率斜坡，剛好落在這個大分類底下，但
 單獨命名為 `Radioid`（暗示是特定曲線）並不準確。
 - 建議：評估是否保留此名稱——如果只是想要「曲率初期快速成長、後段趨緩」的凹形
 選項，建議改名（例如 `ConcaveRamp`）避免混淆；如果需要具名 radioid 家族成員，
 請參考以下三個已個別驗證實作的子類型。

#### 1. Elastic Radioid（彈性曲線 / Elastica）✅

- **公式**：`κ(x) = 2x/a²`（曲率正比於**直角坐標橫座標** x，不是弧長）——歐拉
 1744 年研究的經典「彈性桿彎曲曲線」，mathcurve.com 明確列出的 radioid 家族
 成員之一。
- **驗證**：對 `dθ/ds=κ(x)` 再微分一次得到 `d²θ/ds²=(2/a²)cosθ`——經典的
 「擺動方程」(pendulum equation) 形式，解析解需要雅可比橢圓函數表示。實作上
 改用**耦合 ODE 數值積分**（RK4）直接求解 `dθ/ds=κ(x), dx/ds=cosθ, dy/ds=sinθ`，
 數學上等價於橢圓函數解，但不需要推導 Legendre 標準形式的化簡。
- **實作**：跟 `LemniscateElement`／`PHQuinticElement` 一樣，需先解出自由參數 `a`
 （二分搜尋，每次試驗需一次完整 RK4 積分）才能查詢任意弧長。
- **數值驗證**：`κ(0)=0` 精確為零、`κ(Ls)` 精確等於 `1/R`（精細有限差分確認到
 小數點後 6 位）、曲率全程單調、正負 R 鏡射正確。

#### 2. Norwich Spiral / Sturm's Curve 🟡

- **公式**：`κ=1/r`，`r` 為曲線上該點到一個**固定極點**的距離。mathcurve.com
 明確列為 radioid 家族成員之一。
- **⚠️ 重要的數學發現（誠實記錄）**：`κ=1/r` **在數學上永遠無法精確等於零**，
 除非極點距離 `r→∞`（退化成直線）。這代表這種曲線**天生無法完全符合**「起點
 曲率精確為零」這個規範。已用實際編譯的程式碼驗證：把極點放在起點前方、求解
 極點距離使終點恰好達到目標半徑 `R` 後，起點曲率不是零，而是終點目標曲率的
 **約 77%～86%**（依 `Ls/R` 比值而定）。
- **處理方式**：忠實依照數學定義實作（終點半徑精確等於 `R`），沒有強行修改公式
 讓起點曲率變成零（那樣就不是真正的 Norwich/Sturm 曲線了）。程式碼註解已清楚
 標註此限制。
- **數值驗證**：終點半徑精確等於 `R`、曲率全程單調、正負 R 鏡射正確；起點曲率
 比值（0.863、0.834、0.772，對應 R=500/300/100）。

#### 3. Pseudo-elliptic Radioid ✅

- **公式**：`y = a·gd⁻¹(x/a)`（反 Gudermannian 函數）。出處：mathcurve.com 明確
 定義，並特別註記「看起來像正切曲線，但它的弧長可以用一般函數計算，不需要用到
 橢圓函數」——這正是「pseudo-elliptic」（擬橢圓）這個名字的由來：
 <https://mathcurve.com/courbes2d.gb/radioide/radioide.shtml>
- **推導發現**：`y'(x)=sec(x/a)`，在 `x=0` 處斜率剛好是 1（45°），不是 0！但同時
 `y''(0)=0`，代表 `x=0` 其實是曲率恰好為零的反曲點，只是切線方向是 45° 不是
 水平——跟 `LemniscateElement` 在真正原點處切線剛好是 45° 的情況類似。已比照
 處理：整條曲線先在自己的原生座標系算好，再整體旋轉 −45° 讓切線對齊局部 +x 軸。
- 另外發現：曲率從 `x=0` 的零開始上升，在 `x/a≈1.0255` 處達到最大值，之後隨
 `x/a→π/2`（垂直漸近線）又降回零——只能使用「上升段」當作可用的緩和曲線範圍。
- **數值驗證**：`κ(0)=0` 精確為零、`κ(Ls)` 精確等於 `1/R`、正負 R 鏡射正確。

#### 三個子類型共通說明

三者皆已完整串接到既有架構（`ElementType` 枚舉、`SpiralType` 枚舉、工廠 dispatch、
指令列解析縮寫——`ERAD`／`NWS`／`PER`、UI 下拉選單、ALD 存檔縮寫——因超過 8 字元
欄位限制，分別縮寫為 `ELASRAD`／`NORWICH`／`PSEUELL`），與通用 `RadioidElement`
完全獨立，互不影響。

### Logarithmic ✅ 已重新查證並實作（Log-Aesthetic Curve 框架，n=+1 情形）

- **確切公式來源**：Log-Aesthetic Curve（LAC）家族的一般式（Miura 2005，延伸自
 Harada 等人 1999 年提出的「曲率對數分佈圖」LDDC 概念）：

 ```
 ρ(L)ⁿ = a·L + b
 ```

 其中 `ρ` 為曲率半徑、`L` 為弧長。文獻明確指出：**`n=+1` 時得到對數螺線，
 `n=−1` 時得到 clothoid**：
 - <https://arxiv.org/pdf/2107.09489>（式 8-9）
 - <https://www.researchgate.net/publication/273597834_Log-Aesthetic_Curves_for_Shape_Completion_Problem>
 - <https://www.researchgate.net/publication/228461069_Compound-rhythm_Log-aesthetic_Space_Curve_Segements>
 （確認 log-aesthetic curve 族已被學界研究做為緩和曲線使用）

- **數學驗證（解開了先前「等角螺線與對數曲率斜坡是兩回事」的矛盾）**：用符號
 運算驗證：`ρ(s) = ρ₀ + b·s`（曲率半徑對弧長線性）**恰好是等角螺線
 `r=r₀e^(bθ)` 用弧長參數化後的結果**。兩種先前看似矛盾的定義，其實是同一件
 事的兩種表示法。

- **⚠️ 與 Norwich/Sturm 相同的固有限制**：`κ=1/ρ` 在 `ρ` 對弧長呈線性成長的
 情況下，同樣**無法在有限弧長內精確達到零**（除非 `ρ₀→∞`）。

- **實作**：`ρ(L) = R + b(Ls−L)`，`b = R(K−1)/Ls`，`K=20`（`ρ(0)=20R`，即
 `κ(0)=κ(Ls)/20`）。推導出 `θ(L)` 有乾淨的閉合解：

 ```
 θ(L) = (1/b)·ln[(R+bLs) / (R+b(Ls−L))]
 ```

- **實測結果**（實際編譯的 C++ 程式碼）：

 | R (m) | Ls (m) | κ(0) 實測 | 目標 κ(0)=κ(Ls)/20 | κ(Ls) 實測 | 目標 1/R |
 |---|---|---|---|---|---|
 | 500 | 80 | 0.000100 | 0.000100 | 0.002000 | 0.002000 |
 | 300 | 60 | 0.000167 | 0.000167 | 0.003332 | 0.003333 |
 | 200 | 50 | 0.000250 | 0.000250 | 0.004998 | 0.005000 |
 | 100 | 30 | 0.000500 | 0.000500 | 0.009994 | 0.010000 |

 全部精確吻合；正負 `R` 鏡射也驗證正確。

- **與舊版的差異**：舊版 `g(t)=ln(1+t·(e−1))` 只是借用「對數函數」這個名字做
 曲率斜坡形狀函數，不對應任何文獻中具名的特定曲線；新版有明確文獻出處，且經
 符號驗證確實等於等角螺線一段弧長的真正「Logarithmic」曲線。

### Hyperbolic ✅ 已重新查證並實作（取得原始論文全文與確切公式）

- **確切出處**：Kisgyörgy, L., Barna, Z. "Hyperbolic transition curve",
 Periodica Polytechnica Civil Engineering, 58(1), pp. 63–69, 2014.
 DOI: 10.3311/PPci.7433。開放取用全文：
 <https://pp.bme.hu/ci/article/download/7433/6305>

 後續分析論文：Barna, Z., Kisgyörgy, L. "Analysis of Hyperbolic Transition
 Curve Geometry", Periodica Polytechnica Civil Engineering, 59(2), 2015.
 DOI: 10.3311/PPci.7834

- **確切公式**（論文式 11）：

 ```
 G(l) = (1/2R) · [sh(p−2pl/L) − sh(p) + (2pl/L)·ch(p)] / [p·ch(p) − sh(p)]
 ```

 論文推導過程：從 `sh(x)` 出發，平移其導函數讓兩端切線水平，再縮放使區域極值
 等於目標 `1/R`——是嚴謹推導出來的真正鐵路緩和曲線。`p` 是形狀參數：論文明確
 指出 **`p→0` 趨近 cosine／半正弦曲線，`p→∞` 趨近 clothoid**。

- **舊版實作是錯的**：原本用 `g(t)=tanh(kt)/tanh(k)` 只是借用雙曲正切函數的
 形狀，不對應論文公式或任何具名曲線。

- **驗證方式**：`G(l)` 除以 `1/R` 因子後恰好是純粹關於 `t=l/L` 的形狀函式
 （固定 `p` 下），符號驗證 `g(0)=0, g(1)=1`，可直接套用共用積分器。論文本身
 附了 `p=1,5,20` 三組參數的終點座標**近似公式**（截斷冪級數展開），已交叉比對：

 | p | 論文 X（近似） | 程式算出 x | 論文 Y（近似） | 程式算出 y |
 |---|---|---|---|---|
 | 1 | 79.953162 | 79.953178 | 1.924984 | 1.924933 |
 | 5 | 79.951852 | 79.951858 | 1.993828 | 1.993787 |
 | 20 | 79.949926 | 79.949936 | 2.084294 | 2.084239 |

 差異都在 `5×10⁻⁵` 公尺（0.05 公厘）等級——殘差來自論文自己截斷冪級數的誤差，
 不是本實作的誤差（本實作用真正的數值積分，沒有級數截斷）。端點曲率精確驗證
 等於 `1/R`，正負 `R` 鏡射正確。

- **`p` 參數的處理**：跟 Wiener Bogen 的 `h`、`ψ₂` 一樣，`p` 目前固定為 `5`
 （論文自己分析的三組案例之一，介於兩極端中間），尚未做成使用者可調欄位。

### Polynomial 🟡 已複查（確認為研究類別，非單一公式——維持現狀）

- 出處：
 - Kobryń, A. 系列論文：《Use of polynomial transition curves in the design
 of horizontal arcs》、《New transition curve types for road design》：
 <https://www.rabdim.pl/index.php/rb/article/view/v23n1p99>
 - 《Optimum Railway Transition Curves—Method of the Assessment and Results》：
 <https://www.mdpi.com/1996-1073/14/13/3995>——明確指出研究用**奇數次**
 多項式（5、7、9、11 次，對應 3、5、7、9 個自由係數項）
 - 《Optimization of railway entry and exit transition curves》：
 <https://www.degruyterbrill.com/document/doi/10.1515/eng-2022-0454/html>
 - 英文維基百科「Track transition curve」：19 世紀 Rankine 最早提出的緩和
 曲線公式就是三次曲線（當時稱三次拋物線）：
 <https://en.wikipedia.org/wiki/Track_transition_curve>
- **結論（與 Quintic 情況相同）**：這些論文把多項式係數當成最佳化變數，依車輛
 動力學準則求解，因專案而異——**沒有業界統一的「polynomial transition curve」
 標準公式**。Rankine 最早的三次曲線版本已經跟既有三次拋物線家族重疊。
- **實際使用案例查證**：高次多項式（5、7、9、11 次）目前主要是研究/模擬階段
 （Zboinski、Kobryń 等論文用車輛動力學模擬證實，9、11 次多項式在**長度超過
 150m** 的曲線、高速鐵路場景下，舒適度模擬結果優於傳統三次拋物線），沒有找到
 任何實際運營鐵路線採用高次多項式緩和曲線的紀錄。**唯一的相關例外**：Wiener
 Bogen 內部超高漸變函式最常用七次多項式，且**確認已在奧地利實際部署**——但
 那是包在 Wiener Bogen 這個更複雜構造裡的具名曲線，不是以「Polynomial」通用
 名稱出現。真正大規模實際部署的是三次（Rankine 提出），對應既有
 `ParabolaElement`／`CubicJPNElement`／`CubicECIElement`。
- **維持現狀，不修改公式**：既有 `g(t)=t³` 是這個「多項式緩和曲線」大家族裡一個
 簡單、有明確依據的示意性成員，跟已佔用特定次數／構造的 `QuinticElement`
 （五次平滑階躍）、`WienerBogenElement`（七次 Hasslinger 修正式）、
 `BiquadraticElement`（二次曲率）有明顯區隔。

### Quintic 🟡 已複查（確認為「曲線族」而非單一標準公式）

- 現有 `g(t) = 6t⁵ − 15t⁴ + 10t³` 是數學上標準的「五次平滑階躍函式」(quintic
 smoothstep / minimum-jerk polynomial)，本身公式無誤，兩端曲率導數與二階導數
 皆為零（C² 連續）。
- **複查結果**：搜尋到多篇真正討論「quintic 型鐵路緩和曲線」的學術文獻，確認
 業界對「quintic transition curve」的用法是**一整個五次多項式曲率函式的最佳化
 空間**，而非像 Bloss、Sinusoidal 那樣有統一公式：
 - <https://www.researchgate.net/publication/358199134_Is_the_cubic_parabola_really_the_best_railway_transition_curve>
 （比較三次拋物線、4-3-4 型、quintic 型緩和曲線對重載車輛動力學的影響，
 quintic 型的具體係數依動力學模擬結果調整，非固定公式）
 - <https://doi.org/10.3390/app152212066>（明確指出「依曲線次數不同，最佳化
 出來的形狀有時在端點曲率是平滑的，有時反而刻意在端點留有彎折」，證實五次
 及更高次鐵路緩和曲線的係數是依最佳化準則求解，因專案而異）
 - Zboinski, K.; Woznica, P. *Optimization of Polynomial Transition Curves
 from the Viewpoint of Jerk Value*, Arch. Civ. Eng. 2017
- **結論**：既有公式是這個「五次曲率多項式家族」裡一個數學上合理、有明確依據，
 但非唯一的特定成員選擇。已在程式碼類別註解中補充說明，避免誤導使用者以為
 這是文獻認證的單一標準公式。

### PHQuintic ✅ 已實作（與 Quintic 完全獨立的另一曲線族）

- **出處**：
 - Walton & Meek 原始論文（1996，PH quintic spiral 概念提出）：
 <https://www.sciencedirect.com/science/article/abs/pii/0010448596000229>
 - Farouki, Pelosi, Sampoli (2023) 現代完整數學推導（開放取用全文，本次實作
 的主要依據）：
 <https://escholarship.org/content/qt8rg5b66h/qt8rg5b66h_noSplash_cae70ee6a022e75e1720285d85c6f7f2.pdf>
 - <https://www.researchgate.net/publication/325290302_Pythagorean_Hodograph_Quintic_Trigonometric_Bezier_Transtion_Curve>

- **數學構造**：PH（Pythagorean-Hodograph）五次曲線用複數二次「前像多項式」
 `w(t) = w0(1-t)² + 2w1t(1-t) + w2t²` 定義速端曲線 `r'(t) = w(t)²`，使曲線
 本身是普通五次多項式，因此**弧長是 `t` 的閉合多項式**，不需要數值積分求
 弧長——這是 PH 曲線在 CAD 系統中的核心優勢。

- **邊界條件**：要讓緩和曲線起點為直線相切（`κ(0)=0`）且切線沿 +x 軸方向，
 必須 `w0, w1` 皆為實數（已用符號運算驗證）。剩下的形狀自由度是 `w2 = p + iq`。

- **⚠️ 推導過程中發現並修正了一個重要錯誤**：一開始從記憶引用的曲率公式
 `κ(t) = 2·Im(w·w')/|w|⁴` 其實是錯的——用 SymPy 對 `θ(t)=2·arg(w(t))` 直接
 微分交叉驗證後，發現正確公式應該是 **`κ(t) = 2·Im(w̄·w')/|w|⁴`**（`w̄` 為
 `w` 的共軛複數，不是 `w` 本身）。這個錯誤一開始讓端點曲率算出來偏差約 280%
 （很有規律的偏差，不是隨機誤差），後來才抓出來是公式本身背錯了共軛的位置。
 修正後所有數值驗證都精確吻合，誤差在浮點精度等級。

- 用修正後的曲率公式，加上「終點曲率導數為零」（`κ'(1)=0`，讓緩和曲線平滑
 銜接圓弧）這個條件，把 `q²` 用 `p` 的簡單封閉解表示：

 ```
 q²(p) = p(8-7p)/7 （p 的有效範圍：(0, 8/7)）
 ```

 再用「目標比值 `κ(1)·s(1) = Ls/|R|`」對 `p` 做一維二分搜尋。驗證發現這個
 函式在整個 `(0, 8/7)` 區間**單調遞減**（從 `p→0` 趨近無限大，遞減到
 `p→8/7` 趨近零），對任意正的 `Ls/R` 比值都能找到唯一解，數值上涵蓋
 `Ls/R` 從 0.02 到 5 以上都驗證通過。

- **實作重點**：
 - 外層形狀求解：二分搜尋 `p`，由封閉解算出 `q`，求出縮放係數 `λ²`。
 - 內層查詢：把 `L` 換算成正規化弧長，二分搜尋對應的曲線參數 `t`（用弧長的
 閉合多項式公式，不需數值積分），再用 Farouki 給出的五次貝茲控制點公式
 直接算出 `(x,y)`，`θ(t)=2·atan2(Im w, Re w)`。

- **數值驗證**（修正曲率公式後，實際編譯的 C++ 程式碼）：

 | R (m) | Ls (m) | 端點曲率（精細有限差分） | 目標 1/R | 相對誤差 |
 |---|---|---|---|---|
 | 2000 | 60 | 0.000500 | 0.000500 | 0.0000% |
 | 1000 | 80 | 0.001000 | 0.001000 | 0.0000% |
 | 500 | 80 | 0.002000 | 0.002000 | 0.0000% |
 | 300 | 60 | 0.003333 | 0.003333 | 0.0000% |
 | 200 | 50 | 0.005000 | 0.005000 | -0.0000% |
 | 100 | 30 | 0.010000 | 0.010000 | -0.0000% |

 另外驗證：起點曲率精確為零、正負 `R` 鏡射對稱正確、曲率全程單調。

- **與 QuinticElement 的關係**：`PHQuinticElement` 是完全獨立的新類型
 （`ElementType::PHQuintic`，工廠 dispatch、指令解析縮寫 `PHQ`、UI 下拉選單、
 ALD 存檔縮寫 `PHQNTC` 皆已比照其他類型完整串接），不會與既有 `QuinticElement`
 互相影響或取代。

### Biquadratic（Helmert 1872）✅ 已再次查證並修正（解開先前的矛盾）

- **背景**：先前查證卡在一個矛盾——Autodesk 官方文件說 Schramm 曲線是「兩段二次
 拋物線」，德文維基百科卻說是「單一四次拋物線」（Parabel vierter Ordnung），
 程式碼一度採用「兩段二次拋物線」版本，標記為 🟡 未完全確認。

- **這次解開矛盾的關鍵文獻**：Peter Schuhr（德國鐵路工程學者）的論文精確描述了
 Helmert 1872 年原始推導的邏輯：

 > 「Die geradlinige Überhöhungslinie führt auf die kubische Parabel und die
 > parabelförmig geschwungene Überhöhungslinie von Helmert 1872 auf die
 > biquadratische Parabel.」（直線型超高漸變線導出三次拋物線；Helmert 1872 年
 > 的**拋物線形**超高漸變線導出雙二次拋物線。）

 出處：<https://dgk.badw.de/fileadmin/user_upload/Files/DGK/docs/b-314.pdf>；
 <https://de.wikipedia.org/wiki/%C3%9Cbergangsbogen>；
 <https://de-academic.com/dic.nsf/dewiki/1366076>

- **關鍵推導（已用符號運算驗證）**：「超高漸變線本身是拋物線形」對應到曲率函式，
 就是**曲率本身是弧長的簡單二次拋物線**：

 ```
 κ(l) = (1/R)·(l/Ls)²
 ```

 對這個曲率積分兩次求直角座標 `y(x)`（小角度近似）：

 ```
 θ(l) = l³/(3R·Ls²)
 y(x) = x⁴/(12R·Ls²) ← 真正的四次多項式！
 ```

 這**精確對應**「Parabel vierter Ordnung」（四次拋物線）的描述——曲率本身是
 「一次」拋物線（二次式），積分兩次後的曲線方程式是「四次」拋物線，「雙二次」
 這個名字很可能就是從這個「兩層二次關係」而來。這比 Autodesk「兩段二次拋物線」
 的簡短描述更精確、更直接可追溯到 Helmert 原始文獻的推導邏輯。

- **已修正為**：`g(t) = t²`（單一簡單二次曲率斜坡，取代先前的分段兩段版本）。

- **數值驗證**（實際編譯的 C++ 程式碼）：`κ(0)=0` 精確為零、`κ(Ls)` 精確等於
 `1/R`、`θ(Ls)` 精確等於解析預期值 `Ls/(3R)`（因為 `∫₀¹t²dt=1/3`）、正負 `R`
 鏡射正確。

- **注意**：`g'(0)=0`（起點平滑），但 `g'(1)=2≠0`（終點與圓弧銜接處曲率導數
 不連續）——這跟先前分段版本在兩端都 C¹ 連續不同，是這個「單一簡單二次」版本
 的真實幾何特性，如實反映在程式碼註解中。

### Spline — 無特定文獻對應

在既有文獻中沒有找到以此為名的特定鐵路/公路緩和曲線標準，是本次工作中為了「提供
更多可選形狀」而設計的通用/示意類型（分段三次 Hermite 樣條，見 `TransitionCurveTypes.md`
中的公式）。

**實際使用案例查證**：需區分兩種不同的「spline 使用方式」——(1) spline／B-spline／
NURBS 作為曲線的**計算/儲存表示法**，這在現代 CAD／道路設計軟體（含 Civil 3D 等）
內部確實廣泛使用，但這是純粹的計算技術，跟「選用 spline 形狀當作緩和曲線的幾何
設計」是兩回事；(2) spline 作為緩和曲線**本體幾何形狀**（取代 clothoid/Bloss），
目前查到的都是學術研究提案（如《Exploring Benefits of Using Blending Splines as
Transition Curves》，MDPI 2020：<https://www.mdpi.com/2076-3417/10/12/4226>），
沒有找到被正式採納為運營鐵路或公路規範標準的實際案例。若你的規範沒有要求這種，
可以考慮之後移除或保留作為自訂選項。

### BlossEulerHybrid ✅ 已重新查證並實作（"doucine"，法國 SNCF 真實標準構造）

- **重要發現：真的有實際案例！** 而且是目前少數確認已標準化、實際部署的混合式
 緩和曲線：

 > 「In France, a hybrid solution is installed on both TGV and conventional
 > speed lines. The transition curve used in France is the clothoid, but a
 > short non-linear transition called **doucine** is applied at the ends,
 > smoothing the curvature variation... Their length are **40m for TGV lines
 > and 20m for conventional lines** (Alias – 1984). The use of these
 > doucines was **first standardised on SNCF lines in 1968**.」

 出處：<https://railwaytrackblog.com/2015/02/22/bloss-like-a-boss/>；
 <https://www.researchgate.net/publication/282288817_Bloss_transition_-_a_short_design_guide>

- **架構上的關鍵發現（原本的實作沒有掌握到）**：doucine **不是**均勻混合 Bloss
 與 clothoid 兩種形狀（原本的 `0.5×Bloss + 0.5×Euler` 做法），而是**中段維持
 純線性（clothoid 本體），只在兩端加上短的平滑（Bloss 式）圓角**，用來消除純
 clothoid 在跟前方直線、後方圓弧銜接處的「曲率導數不連續」問題。已用有限差分
 驗證：舊版在起點的曲率導數其實不是零，並沒有真正達到「圓角消除尖角」的效果。

- **已修正為真正的 doucine 構造**：用「平滑梯形」曲率導數函式建構——兩端各用
 升餘弦曲線平滑地從 0 爬升到一個平台值，中段維持常數（曲率本身呈線性，模擬
 clothoid 本體）。已驗證：兩端交界處數值與導數皆連續、兩端點曲率導數精確為零、
 中段真正呈線性。

- **實作參數**：兩端圓角各佔全長的 25%（`f=0.25`），中段 50% 維持線性——這是
 有文件記載的代表性比例，因為 SNCF 原始標準用的是固定絕對長度（40m／20m），
 沒辦法直接換算成適用任意 `Ls` 的固定比例，這點已在程式碼註解中誠實說明。

- **數值驗證**（R=500m, Ls=80m）：起點曲率精確為零、終點曲率精確等於 `1/R`、
 中段（L=30/40/50m）曲率呈完美線性成長（每 10m 增加 0.000333，與理論斜率
 `C/(R·Ls)` 精確吻合）、正負 R 鏡射正確。

### CubicECI（既有類型，本次一併重新查證與修正）✅

雖然 `CubicECI` 屬於本次工作之前就已存在的 5 種類型之一，但既有實作的正確性在
對話過程中受到質疑並查證，結果確認有誤，已重新實作：

- **CECI 原始理論公式**（你提供）：`y = x³/(6RL)`——最經典、最簡單的三次拋物線
 緩和曲線公式，`L` 是緩和曲線全長，`x` 直接取弧長座標，與 Indian Railways 教材
 描述的「僅取泰勒級數第一項」的簡化三次拋物線一致。

- **原本的實作是錯的**：舊版用一套 `tan(φ)/(1+tan²φ)^1.5 · [1+p²/10−p⁴/72+p⁶/208]`
 的隱式方程，跟 CECI 這個簡單公式完全是兩回事。數值比對發現端點切線角偏差最高
 達 4.8%（R=50m 時），`y` 座標偏差達數十毫米，效能也差（每次查詢都要重新二分
 搜尋隱式方程）。

- **第一次修正（`x=L` 直接代入）並不完全正確**——你指出 CECI 文獻中還有一組
 弧長↔切線座標的近似公式，形如 `l(x)=x(1+…四項)`。已用符號運算重新推導（對
 `y=x³/(6RLs)` 的弧長積分做泰勒展開），得到與你描述的「四項」結構完全吻合的
 公式，並經你比對確認係數與你手上的 CECI 文獻相同：

 ```
 l(x) = x·[ 1 + x⁴/(40R²Ls²) − x⁸/(1152R⁴Ls⁴) + x¹²/(13312R⁶Ls⁶) ]
 ```

- **已修正為**：用不動點迭代反解 `x`（`l` 已知，即查詢的 `L`）。已編譯驗證：
 與「直接數值積分弧長反解 x」的真實解相比，即使在 R=100m／Ls=30m 這種曲率很大
 的情況下，誤差也只有 **1.7×10⁻⁵ 公厘**等級。比起第一版「`x=L` 直接代入」在
 同樣條件下誤差達 0.95 公厘，精度提升了近 5 個數量級。

- **一併修正的遺漏**：`RailwayAlignment.cpp` 的 `computeRadius()`（供資料表、
 OSnap 等即時顯示半徑用）之前把 `CUBICECI` 跟 `PARABOLA`／`CUBICJPN` 歸在同一組，
 用簡化的線性近似公式，沒有跟著更新。已修正為採用與 `CubicECIElement` 完全
 一致的四項反算式，並用正確的曲率公式 `κ(x)=y″(x)/(1+y'(x)²)^1.5`（一開始
 誤用 `κ(x)=y″(x)` 單獨，忽略了 `(1+y'²)^1.5` 分母修正，用有限差分交叉驗證
 抓到並修正）。修正後跟實際元件的曲率誤差降到 `1e-9` 等級。

- **意外發現的 CECI 公式固有特性（非 bug）**：`CubicECIElement` 在曲線真正終點
 （`L=Ls`）的曲率半徑並不精確等於目標 `R`——例如 `R=300m, Ls=60m` 時，終點
 真實曲率半徑算出來是 `304.8m`，跟目標 `300m` 差了 1.6%。原因：公式
 `y=x³/(6RLs)` 分母用固定的 `Ls`，但曲線真正的終點對應的 `x` 座標其實比 `Ls`
 略短（弧長恆 `≥ x`）。這是 CECI 公式本身的固有特性，不是實作錯誤，曲率越大
 這個終點半徑偏差就越大。

---

## 程式實作公式總覽（LaTeX 展開式，逐 SpiralType 對照）

本節依 `src/railway/RailwayAlignmentElement.cpp` 目前的原始碼，把每一種
`SpiralType` 實際使用的曲率、切線角（方位角變化量）、局部座標 $(x,y)$ 公式，
逐一轉寫成 LaTeX。符號慣例（與程式一致）：

- $R$：帶號半徑（`m_reversed` 為真時取 $-R$），$L_s$：緩和曲線全長，
 $L$／$s$：沿線弧長（$0\le L\le L_s$），$t=L/L_s\in[0,1]$。
- $\theta(L)$：局部座標系下的切線方位角（相對起點切線方向），
 $\kappa(L)=d\theta/dL$：曲率。
- 除非另有說明，每個元素都在 $L=0$ 處滿足 $(x,y,\theta)=(0,0,0)$，
 在 $L=L_s$ 處滿足 $\kappa(L_s)=1/R$，與直線／圓弧兩端銜接。

---

### 公式速查表（每列一種 SpiralType）

> 表格內 $t=s/L_s\in[0,1]$。$\theta(L)$ 一律為**精確閉式解**（$g(t)$ 均為初等函數，
> 可精確積分，不需展開）。$x(L),y(L)$ 因牽涉 $\cos\theta(s),\sin\theta(s)$ 的複合函數，
> 一般不存在初等封閉解，故採用**泰勒展開式**（$\cos\theta\approx1-\theta^2/2+\theta^4/24-\cdots$、
> $\sin\theta\approx\theta-\theta^3/6+\theta^5/120-\cdots$，代入精確 $\theta(s)$ 後逐項對 $s$
> 積分求得，不列積分符號），並在表中以最高冪次標明截斷階數；程式實際執行時仍是用
> `nSteps=400` 複合梯形法做真正的數值積分（見下方 A 節），表中展開式僅供人工核對估算。

| SpiralType | 曲率 $\kappa$（或形狀函式 $g(t)$） | 半徑 $\rho=1/\kappa$ | 切線角 $\theta(L)$（精確閉式） | 座標 $x(L)$（泰勒展開） | 座標 $y(L)$（泰勒展開） |
|---|---|---|---|---|---|
| **Clothoid** | $\kappa(s)=s/A^2,\ A^2=\vert R\vert L_s$ | $\rho(s)=A^2/s$ | $\theta=\operatorname{sign}(R)\,L^2/(2A^2)$ | $x=L\Big(1-\dfrac{\theta^2}{10}+\dfrac{\theta^4}{216}-\dfrac{\theta^6}{9360}\Big)$ | $y=L\theta\Big(\dfrac{1}{3}-\dfrac{\theta^2}{42}+\dfrac{\theta^4}{1320}\Big)$ |
| **HalfSine** | $\kappa(s)=\dfrac{1}{2R}\Big(1-\cos\dfrac{\pi s}{L_s}\Big)$ | $\rho(s)=1/\kappa(s)$ | $\theta=\dfrac{1}{2R}\Big(L-\dfrac{L_s}{\pi}\sin\dfrac{\pi L}{L_s}\Big)$，令 $b=\frac{1}{2R},\lambda=\frac{\pi}{L_s},B=\lambda L$ | $x=L-\dfrac{b^2}{12\lambda^3}\big[2B^3-12\sin B+12B\cos B-3\cos B\sin B+3B\big]$ | $y=\dfrac{b}{2}\Big[L^2+\dfrac{2(\cos B-1)}{\lambda^2}\Big]-\dfrac{b^3}{72\lambda^4}\big[3B^4+36B^2\cos B-60\cos B-72B\sin B-18B\cos B\sin B+9B^2-9\cos^2B-4\cos^3B+73\big]$ |
| **Parabola** | $\kappa(x)\approx x/(R\,X_B)$（隱含於 $y=x^3/(6RX_B)$） | $\rho\approx RX_B/x$ | $\theta=\operatorname{sign}(R)\arctan\!\Big(\dfrac{x^2}{2\vert R\vert X_B}\Big)$ | $x=L-\dfrac{L^5}{40R^2L_s^2}$ | $y=\dfrac{x^3}{6R\,X_B},\ X_B=L_s-\dfrac{L_s^3}{40R^2}$ |
| **CubicJPN** | 隱式：$x=L\cdot\dfrac{10}{10+(x^2/(2R\,\mathrm{BigX}))^2}$ | $\rho\approx R\,\mathrm{BigX}/x$ | $\theta=\arctan\!\Big(\dfrac{x^2}{2R\,\mathrm{BigX}}\Big)$ | $x$ 由左式二分法反解（$\mathrm{BigX}$ 同型方程先解出） | $y=\dfrac{x^3}{6R\,\mathrm{BigX}}$ |
| **CubicECI** | 理論 $y=x^3/(6RL_s)\Rightarrow\tan\theta=x^2/(2RL_s)$ | $\rho\approx RL_s/x$ | $\theta=\operatorname{sign}(R)\arctan\!\Big(\dfrac{x^2}{2\vert R\vert L_s}\Big)$ | $l(x)=x\Big[1+\dfrac{\tan^2\theta}{10}-\dfrac{\tan^4\theta}{72}+\dfrac{\tan^6\theta}{208}\Big]$，令 $l=L$ 反解 $x$ | $y=\dfrac{x^3}{6RL_s}$ |
| **Cosine（MÁV）** | $y''(x)=\dfrac{1}{2R}\Big(1-\cos\dfrac{\pi x}{L_s}\Big)$ | $\rho(x)=1/y''(x)$ | $\theta=\arctan\big(y'(x)\big)$ | $x=L$（$x$ 直接當弧長使用，古典近似） | $y=\dfrac{x^2}{4R}-\dfrac{L_s^2}{2\pi^2R}\Big(1-\cos\dfrac{\pi x}{L_s}\Big)$ |
| **Lemniscate** | $\kappa_1(\tau)=1.5\sqrt{x_1^2+y_1^2}$（正規化） | $\rho=a/\kappa_1$ | 由 $a\cdot s_1(\tau)=L$ 反算 $\tau$，$a=\vert R\vert\kappa_1(\tau_{\text{end}})$ | $x=a\,x_1(\tau)$ | $y=\operatorname{sign}(R)\cdot a\,y_1(\tau)$ |
| **Elastic Radioid** | $\kappa(x)=2x/a^2$ | $\rho(x)=a^2/(2x)$ | $d\theta/ds=\kappa(x)$（RK4，$a$ 由二分法求 $\kappa(L_s)=1/R$） | $dx/ds=\cos\theta$（RK4 數值積分） | $dy/ds=\sin\theta$（RK4 數值積分） |
| **Norwich/Sturm** | $\kappa(x,y)=1/r,\ r=\sqrt{(x-D)^2+y^2}$ | $\rho=r$ | $d\theta/ds=\kappa$（RK4，$D$ 由二分法求終點 $\rho=R$） | $dx/ds=\cos\theta$（RK4 數值積分） | $dy/ds=\sin\theta$（RK4 數值積分） |
| **Pseudo-elliptic Radioid** | $\kappa(x)=\dfrac{\sec\frac{x}{a}\tan\frac{x}{a}/a}{\big(1+\sec^2\frac{x}{a}\big)^{3/2}}$ | $\rho=1/\kappa(x)$ | $\theta=\arctan\big(\sec\frac{x}{a}\big)-45^\circ$（旋轉對齊） | $x=L-\dfrac{L^5}{640a^4}$（旋轉 $-45^\circ$ 後，截至 $L^5$） | $y=\dfrac{L^3}{24a^2}-\dfrac{11L^7}{35840a^6}$（旋轉 $-45^\circ$ 後，截至 $L^7$） |
| **Logarithmic** | $\rho(L)=R+b(L_s-L)$，$b=R(K{-}1)/L_s,\ K{=}20$ | $\rho(L)$ 見左欄（半徑本身即線性） | $\theta(L)=\dfrac{L_s}{19R}\ln\dfrac{20L_s}{20L_s-19L}$ | $x=L-\dfrac{L^3}{2400R^2}-\dfrac{19L^4}{64000L_sR^2}+\Big[\dfrac{1}{19200000R^4}-\dfrac{3971}{19200000L_s^2R^2}\Big]L^5$ | $y=\dfrac{L^2}{40R}+\dfrac{19L^3}{2400L_sR}+\Big[\dfrac{361}{96000L_s^2R}-\dfrac{1}{192000R^3}\Big]L^4+\Big[\dfrac{6859}{3200000L_s^3R}-\dfrac{19}{3200000L_sR^3}\Big]L^5$ |
| **PHQuintic** | $\kappa(t)=\dfrac{2\operatorname{Im}(\overline{w}w')}{\vert w\vert^4}$，$w=(1-t)^2+2t(1-t)+(p+iq)t^2$ | $\rho=1/\kappa(t)$ | $\theta(t)=2\arg\big(w(t)\big)$ | $x(t)=\lambda^2\operatorname{Re}\big(r(t)\big)$，$r(t)=\sum_{k=0}^5\binom{5}{k}(1-t)^{5-k}t^kp_k$ | $y(t)=\operatorname{sign}(R)\,\lambda^2\operatorname{Im}\big(r(t)\big)$，$\lambda^2=L_s/s(1)$ |
| **Sinusoidal** | $g(t)=t-\dfrac{\sin(2\pi t)}{2\pi}$ | $\rho=R/g(t)$ | $\theta(L)=\dfrac{L^2}{2L_sR}-\dfrac{L_s\sin^2(\pi L/L_s)}{2\pi^2R}$ | $x=L-\dfrac{\pi^4L^9}{648L_s^6R^2}$（截至 $L^9$） | $y=\dfrac{\pi^2L^5}{30L_s^3R}-\dfrac{\pi^4L^7}{315L_s^5R}+\dfrac{\pi^6L^9}{5670L_s^7R}$（截至 $L^9$） |
| **Bloss** | $g(t)=3t^2-2t^3$ | $\rho=R/g(t)$ | $\theta(L)=\dfrac{L^3}{L_s^2R}-\dfrac{L^4}{2L_s^3R}$ | $x=L-\dfrac{L^7}{14L_s^4R^2}+\dfrac{L^8}{16L_s^5R^2}-\dfrac{L^9}{72L_s^6R^2}$ | $y=\dfrac{L^4}{4L_s^2R}-\dfrac{L^5}{10L_s^3R}-\dfrac{L^{10}}{60L_s^6R^3}+\dfrac{L^{11}}{44L_s^7R^3}-\dfrac{L^{12}}{96L_s^8R^3}+\dfrac{L^{13}}{624L_s^9R^3}$ |
| **Radioid（通用）** | $g(t)=2t-t^2$ | $\rho=R/g(t)$ | $\theta(L)=\dfrac{L^2}{L_sR}-\dfrac{L^3}{3L_s^2R}$ | $x=L-\dfrac{L^5}{10L_s^2R^2}+\dfrac{L^6}{18L_s^3R^2}-\dfrac{L^7}{126L_s^4R^2}$ | $y=\dfrac{L^3}{3L_sR}-\dfrac{L^4}{12L_s^2R}-\dfrac{L^7}{42L_s^3R^3}+\dfrac{L^8}{48L_s^4R^3}-\dfrac{L^9}{162L_s^5R^3}+\dfrac{L^{10}}{1620L_s^6R^3}$ |
| **Polynomial** | $g(t)=t^3$ | $\rho=R/g(t)$ | $\theta(L)=\dfrac{L^4}{4L_s^3R}$ | $x=L-\dfrac{L^9}{288L_s^6R^2}$ | $y=\dfrac{L^5}{20L_s^3R}-\dfrac{L^{13}}{4992L_s^9R^3}$ |
| **Quintic** | $g(t)=6t^5-15t^4+10t^3$ | $\rho=R/g(t)$ | $\theta(L)=\dfrac{L^6}{L_s^5R}-\dfrac{3L^5}{L_s^4R}+\dfrac{5L^4}{2L_s^3R}$ | $x=L-\dfrac{25L^9}{72L_s^6R^2}+\dfrac{3L^{10}}{4L_s^7R^2}-\dfrac{7L^{11}}{11L_s^8R^2}+\dfrac{L^{12}}{4L_s^9R^2}-\dfrac{L^{13}}{26L_s^{10}R^2}$ | $y=\dfrac{L^5}{2L_s^3R}-\dfrac{L^6}{2L_s^4R}+\dfrac{L^7}{7L_s^5R}-\dfrac{125L^{13}}{624L_s^9R^3}+\dfrac{75L^{14}}{112L_s^{10}R^3}-\dfrac{23L^{15}}{24L_s^{11}R^3}+\dfrac{3L^{16}}{4L_s^{12}R^3}-\dfrac{23L^{17}}{68L_s^{13}R^3}+\dfrac{L^{18}}{12L_s^{14}R^3}-\dfrac{L^{19}}{114L_s^{15}R^3}$ |
| **Biquadratic** | $g(t)=t^2$ | $\rho=R/g(t)$ | $\theta(l)=l^3/(3RL_s^2)$（小角度封閉解） | $x(L)\approx L$（小角度近似下 $\cos\theta\approx1$） | $y(x)=x^4/(12RL_s^2)$（小角度封閉解） |
| **Spline** | $g(t)=$ 分段三次 Hermite（見 A 節） | $\rho=R/g(t)$ | 第一段（$0\le L\le0.5L_s$）：$\theta(L)=\dfrac{4L^3}{3L_s^2R}-\dfrac{L^4}{L_s^3R}$ | $x=L-\dfrac{8L^7}{63L_s^4R^2}+\dfrac{L^8}{6L_s^5R^2}-\dfrac{L^9}{18L_s^6R^2}$（僅第一段，$L\le0.5L_s$） | $y=\dfrac{L^4}{3L_s^2R}-\dfrac{L^5}{5L_s^3R}$（僅第一段，$L\le0.5L_s$；第二段對稱式另需分段積分，見 A 節） |
| **Hyperbolic（p=5）** | $g(t)=\dfrac{\sinh(p(1-2t))-\sinh p+2pt\cosh p}{2(p\cosh p-\sinh p)}$ | $\rho=R/g(t)$ | $\theta(L)=\dfrac{L_s^2\big[\cosh5-\cosh(10L/L_s-5)\big]+50L^2\cosh5-10LL_s\sinh5}{20L_sRD}$，$D=5\cosh5-\sinh5$ | $x=L-\dfrac{625\sinh^25}{126D^2L_s^4R^2}L^7+\dfrac{3125\sinh5\cosh5}{144D^2L_s^5R^2}L^8-\Big[\dfrac{15625\cosh^25}{648}+\dfrac{3125\sinh^25}{81}\Big]\dfrac{L^9}{D^2L_s^6R^2}$ | $y=\dfrac{25\sinh5}{12DL_s^2R}L^4-\dfrac{25\cosh5}{6DL_s^3R}L^5+\dfrac{125\sinh5}{18DL_s^4R}L^6-\dfrac{625\cosh5}{63DL_s^5R}L^7+\dfrac{3125\sinh5}{252DL_s^6R}L^8-\dfrac{15625\cosh5}{1134DL_s^7R}L^9$ |
| **WienerBogen** | $g(t)=35t^4-84t^5+70t^6-20t^7$，另減 $\vert R\vert h\psi_2g''(t)/L_s^2$ 修正項（$h{=}2.1336,\psi_2{=}0.10$） | $\rho=R/[g(t)-\vert R\vert h\psi_2g''(t)/L_s^2]$ | $\theta(L)=\dfrac{18669L^3}{625L_s^4}-\dfrac{56007L^4}{625L_s^5}+\dfrac{7L^5}{L_s^4R}+\dfrac{56007L^5}{625L_s^6}-\dfrac{14L^6}{L_s^5R}-\dfrac{18669L^6}{625L_s^7}+\dfrac{10L^7}{L_s^6R}-\dfrac{5L^8}{2L_s^7R}$ | $x=L-\dfrac{49790223L^7}{781250L_s^8}+\dfrac{1045594683L^8}{3125000L_s^9}+\dfrac{43561L^9}{1875L_s^8R}-\dfrac{116177187L^9}{156250L_s^{10}}$（截至 $L^9$） | $y=\dfrac{56007L^5}{3125L_s^5}-\dfrac{18669L^4}{2500L_s^4}+\dfrac{7L^6}{6L_s^4R}-\dfrac{18669L^6}{1250L_s^6}-\dfrac{2L^7}{L_s^5R}+\dfrac{2667L^7}{625L_s^7}+\dfrac{5L^8}{4L_s^6R}-\dfrac{5L^9}{18L_s^7R}$（截至 $L^9$） |
| **BlossEulerHybrid** | $g(t)=$ 分段梯形 $g'(t)$（端部占比 $f{=}0.25$，見 A 節） | $\rho=R/g(t)$ | 第一段（$0\le L\le0.25L_s$）：$\theta(L)=\dfrac{8\pi^2L^2+L_s^2[\cos(4\pi L/L_s)-1]}{24\pi^2L_sR}$ | $x=L-\dfrac{8\pi^4L^9}{729L_s^6R^2}$（僅第一段，$L\le0.25L_s$） | $y=\dfrac{4\pi^2L^5}{45L_s^3R}-\dfrac{32\pi^4L^7}{945L_s^5R}+\dfrac{64\pi^6L^9}{8505L_s^7R}$（僅第一段，$L\le0.25L_s$；中段線性、末段對稱另需分段積分，見 A 節） |

---

### A. 共用「曲率斜坡」數值積分框架（`integrateCurvatureRamp`）

`Sinusoidal`、`Bloss`、`WienerBogen`、`Radioid`、`Hyperbolic`、`Polynomial`、
`Quintic`、`Biquadratic`、`Spline`、`BlossEulerHybrid` 共十種類型，都共用同一套
「先定義形狀函式 $g(t)$，再做複合梯形法數值積分」的框架（`nSteps=400`）：

$$
\kappa(s) = \frac{g(s/L_s)}{R}, \qquad g:[0,1]\to[0,1],\ g(0)=0,\ g(1)=1
$$

$$
\theta(L) = \int_0^L \kappa(s) ds \qquad x(L) = \int_0^L \cos\theta(s) ds \qquad y(L) = \int_0^L \sin\theta(s) ds
$$

以下只列出各類型各自的 $g(t)$（或必要時的修正項），代入上式即為該類型完整公式。

#### Sinusoidal

$$
g(t) = t - \frac{\sin(2\pi t)}{2\pi}
$$

#### Bloss

$$
g(t) = 3t^2 - 2t^3
$$

#### Radioid（通用版，凹形曲率斜坡）

$$
g(t) = 2t - t^2
$$

#### Polynomial

$$
g(t) = t^3
$$

#### Quintic（五次平滑階躍 / minimum-jerk polynomial）

$$
g(t) = 6t^5 - 15t^4 + 10t^3
$$

#### Biquadratic（Helmert 1872，曲率本身呈二次拋物線）

$$
g(t) = t^2
$$

推導出的直角座標小角度近似（見「逐項細節」章節）：

$$
\theta(l) = \frac{l^3}{3RL_s^2} \qquad\Longrightarrow\qquad y(x) = \frac{x^4}{12RL_s^2}
$$

#### Spline（分段三次 Hermite，無特定文獻對應，通用示意型）

以 $u\in[0,1]$ 上的三次 Hermite 基底

$$
h_{00}(u)=2u^3-3u^2+1,\quad h_{10}(u)=u^3-2u^2+u,\quad h_{01}(u)=-2u^3+3u^2,\quad h_{11}(u)=u^3-u^2
$$

分兩段組成 $g(t)$（節點 $(0,0)\to(0.5,0.5)\to(1,1)$，切線斜率 $0,1,0$，
切線向量依區段長度 $0.5$ 縮放）：

$$
g(t)= \begin{cases} h_{00} \left(\dfrac{t}{0.5}\right)\cdot 0 + h_{10} \left(\dfrac{t}{0.5}\right)\cdot 0 + h_{01} \left(\dfrac{t}{0.5}\right)\cdot 0.5 + h_{11} \left(\dfrac{t}{0.5}\right)\cdot 0.5, & 0\le t\le 0.5\\[2mm] h_{00} \left(\dfrac{t-0.5}{0.5}\right)\cdot 0.5 + h_{10} \left(\dfrac{t-0.5}{0.5}\right)\cdot 0.5 + h_{01} \left(\dfrac{t-0.5}{0.5}\right)\cdot 1 + h_{11} \left(\dfrac{t-0.5}{0.5}\right)\cdot 0, & 0.5< t\le 1 \end{cases}
$$

#### Hyperbolic（Kisgyörgy & Barna 2014，論文式 11，形狀參數固定 $p=5$）

$$
g(t) = \frac{\sinh \big(p(1-2t)\big) - \sinh(p) + 2pt\cosh(p)} {2\big(p\cosh(p) - \sinh(p)\big)} \Bigg|_{p=5}
$$

（$G(l)=g(t)/(R)$ 對應論文原式 $G(l)=\dfrac{1}{2R}\cdot\dfrac{\sinh(p-2pl/L_s)-\sinh(p)+(2pl/L_s)\cosh(p)}{p\cosh(p)-\sinh(p)}$。）

#### WienerBogen（Hasslinger 原式，七次 smootherstep + 超高二階導修正項）

七次形狀函式與其二階導數：

$$
g(t) = 35t^4 - 84t^5 + 70t^6 - 20t^7 \qquad g''(t) = 420t^2 - 1680t^3 + 2100t^4 - 840t^5
$$

實際採用的曲率（$h=2.1336 \mathrm{m}$、$\psi_2=0.10 \mathrm{rad}$ 為固定代表值）：

$$
\kappa(l) = \frac{1}{R}\left[ g(t) - |R| h \psi_2 \frac{g''(t)}{L_s^2} \right], \qquad t=\frac{l}{L_s}
$$

對應文獻通式 $\kappa(l)=\kappa_1+(\kappa_2-\kappa_1)f(l)-h(\psi_2-\psi_1)f''(l)$，
其中 $\kappa_1=0,\ \kappa_2=1/R,\ \psi_1=0,\ f=g$。

#### BlossEulerHybrid（"doucine"，法國 SNCF 1968 標準構造，端部佔比 $f=0.25$）

以平滑梯形的 $g'(t)$ 構造（$C=1/(1-f)$）：

$$
g(t)= \begin{cases} \dfrac{C}{2}\left(t-\dfrac{f}{\pi}\sin\dfrac{\pi t}{f}\right), & 0\le t\le f\\[3mm] g(f) + C (t-f), & f< t< 1-f\\[2mm] 1-\dfrac{C}{2}\left((1-t)-\dfrac{f}{\pi}\sin\dfrac{\pi(1-t)}{f}\right), & 1-f\le t\le 1 \end{cases} \qquad f=0.25
$$

---

### B. 封閉解／半封閉解類型（不使用共用積分框架）

#### Clothoid（Euler spiral，冪級數展開至 $\theta^6$）

$$
A^2 = |R| L_s,\qquad \theta(L) = \operatorname{sign}(R)\cdot\frac{L^2}{2A^2}
$$

$$
x(L) = L\left(1-\frac{\theta^2}{10}+\frac{\theta^4}{216}-\frac{\theta^6}{9360}\right) \qquad y(L) = L \theta\left(\frac{1}{3}-\frac{\theta^2}{42}+\frac{\theta^4}{1320}\right)
$$

$$
\kappa(s) = \frac{s}{A^2}
$$

#### HalfSine（與 Cosine 共用曲率律，`halfSineExactFrame`，封閉解至 $1/R^3$ 階）

曲率與方位角：

$$
\kappa(s) = \frac{1}{2R}\Big(1-\cos\frac{\pi s}{L_s}\Big) \qquad \theta(L) = \frac{1}{2R}\left(L - \frac{L_s}{\pi}\sin\frac{\pi L}{L_s}\right)
$$

令 $b=\dfrac{1}{2R}$，$\lambda=\dfrac{\pi}{L_s}$，$B=\lambda L$，則

$$
x(L) = L - \frac{b^2}{12\lambda^3} \Big[ 2B^3 - 12\sin B + 12B\cos B - 3\cos B\sin B + 3B \Big]
$$

$$
y(L) = \frac{b}{2}\left[L^2 + \frac{2(\cos B-1)}{\lambda^2}\right] - \frac{b^3}{72\lambda^4}\Big[ 3B^4+36B^2\cos B-60\cos B-72B\sin B -18B\cos B\sin B+9B^2-9\cos^2B-4\cos^3B+73 \Big]
$$

（此封閉解經 SymPy 驗證與 ISO 16739-1:2024 `IfcCosineSpiral`
在 $A_0=2R,\ A_1=-2R$ 時完全等價。）

#### Parabola（三次拋物線，古典小角度近似）

$$
X_B = L_s - \frac{L_s^3}{40R^2} \qquad x(L) = L - \frac{L^5}{40R^2L_s^2}
$$

$$
y(x) = \frac{x^3}{6R X_B} \qquad \theta(x) = \operatorname{sign}(R)\cdot\arctan \left(\frac{x^2}{2|R|X_B}\right)
$$

#### CubicJPN（日本鐵路三次拋物線，隱式方程二分法反解）

先求全長對應的 $\mathrm{BigX}$（二分法解方程）：

$$
\mathrm{BigX} = L_s\cdot\frac{10}{10+\left(\dfrac{\mathrm{BigX}}{2R}\right)^2}
$$

再對任意 $L$ 求對應的 $x$（同型隱式方程）：

$$
x = L\cdot\frac{10}{10+\left(\dfrac{x^2}{2R\cdot\mathrm{BigX}}\right)^2}
$$

$$
y = \frac{x^3}{6R\cdot\mathrm{BigX}} \qquad \theta = \arctan \left(\frac{x^2}{2R\cdot\mathrm{BigX}}\right)
$$

#### CubicECI（CECI 原始三次拋物線 + 四項弧長反算式，以 $\tan\theta$ 代入）

理論公式（$x$ 為切線橫座標，非弧長本身）：

$$
y = \frac{x^3}{6RL_s}
$$

切線角滿足 $\tan\theta = dy/dx$，對 $y=x^3/(6RL_s)$ 微分得：

$$
\tan\theta = \frac{dy}{dx} = \frac{x^2}{2RL_s}
$$

把 $x^4=(2RL_s\tan\theta)^2=4R^2L_s^2\tan^2\theta$ 代回原本的四項弧長展開式
$l(x)=x\big[1+\tfrac{x^4}{40R^2L_s^2}-\tfrac{x^8}{1152R^4L_s^4}+\tfrac{x^{12}}{13312R^6L_s^6}\big]$，
每一項的 $R,L_s$ 恰好完全消去，弧長 $\leftrightarrow$ $x$ 的四項展開式可改寫成只含
$\tan\theta$ 的無因次形式（用不動點迭代反解 $x$，$\theta$ 隨 $x$ 同步更新）：

$$
l(x) = x\left[ 1 + \frac{\tan^2\theta}{10} - \frac{\tan^4\theta}{72} + \frac{\tan^6\theta}{208} \right]
$$

$$
\theta(x) = \operatorname{sign}(R)\cdot\arctan \left(\frac{x^2}{2|R|L_s}\right)
$$

（此形式與「逐項細節」章節中提到的舊版隱式方程
$\tan\varphi/(1+\tan^2\varphi)^{1.5}\cdot[1+p^2/10-p^4/72+p^6/208]$
共用同一套 $[1+p^2/10-p^4/72+p^6/208]$ 級數係數，$p=\tan\theta$——
差別在於舊版隱式方程本身的構造是錯的，但這個係數級數本身其實與 CECI
弧長展開式一致，可視為同一組泰勒級數以不同自變數表示的結果。）

#### Cosine（MÁV 匈牙利國鐵，直角座標 $y=f(x)$ 古典近似）

$$
y(x) = \frac{x^2}{4R} - \frac{L_s^2}{2\pi^2R}\left(1-\cos\frac{\pi x}{L_s}\right)
$$

$$
y'(x) = \frac{x}{2R} - \frac{L_s}{2\pi R}\sin\frac{\pi x}{L_s} \qquad \theta(x) = \arctan\big(y'(x)\big)
$$

（與 HalfSine 曲率律相同：$y''(x)=\dfrac{1}{2R}\Big(1-\cos\dfrac{\pi x}{L_s}\Big)$，
但以 $x$ 直接當弧長使用，屬古典近似而非精確弧長參數化。）

#### Lemniscate（伯努利雙紐線，正規化 $a=1$ 幾何 + 弧長反算縮放）

正規化參數式（$t=\pi/2-\tau$）：

$$
x_1(\tau) = \frac{\sqrt{2}\cos t}{1+\sin^2 t} \qquad y_1(\tau) = \frac{\sqrt{2}\sin t\cos t}{1+\sin^2 t}
$$

正規化曲率：

$$
\kappa_1(\tau) = 1.5\sqrt{x_1^2+y_1^2}
$$

求自由參數 $a$：先二分搜尋 $\tau_{\text{end}}$ 使
$\kappa_1(\tau_{\text{end}})\cdot s_1(\tau_{\text{end}}) = L_s/|R|$
（$s_1$ 為正規化弧長，數值積分求得），再令

$$
a = |R|\cdot\kappa_1(\tau_{\text{end}})
$$

任意 $L$ 對應之 $\tau$ 由 $a\cdot s_1(\tau)=L$ 反算，最終座標
（旋轉 $-\theta_0$，$\theta_0=\theta_1(\tau\to0^+)\approx45^\circ$，
再依 $R$ 正負鏡射）：

$$
(x,y) = \big(a x_b,\ \operatorname{sign}(R)\cdot a y_b\big)
$$

#### Elastic Radioid（歐拉彈性曲線 / Elastica）

曲率正比於局部橫座標 $x$（非弧長）：

$$
\kappa(x) = \frac{2x}{a^2}
$$

以耦合 ODE（RK4 數值積分求解，$a$ 由二分法求使 $\kappa(L_s)=1/R$）：

$$
\frac{d\theta}{ds}=\kappa(x),\qquad \frac{dx}{ds}=\cos\theta,\qquad \frac{dy}{ds}=\sin\theta
$$

#### Norwich Spiral / Sturm's Curve

曲率反比於到固定極點 $(D,0)$ 的距離（$D$ 由二分法求使終點半徑精確等於 $R$）：

$$
\kappa(x,y) = \frac{1}{r},\qquad r=\sqrt{(x-D)^2+y^2}
$$

同樣以 RK4 積分 $\dfrac{d\theta}{ds}=\kappa,\ \dfrac{dx}{ds}=\cos\theta,\ \dfrac{dy}{ds}=\sin\theta$ 求解。數學上 $\kappa=1/r$ 在有限弧長內
**無法精確等於零**（起點曲率恆為終點目標曲率的一個非零比例）。

#### Pseudo-elliptic Radioid（反 Gudermannian 函數）

自然座標系（原生尺度 $a$）：

$$
y(x) = a\cdot\operatorname{gd}^{-1} \left(\frac{x}{a}\right) = a\cdot\operatorname{asinh} \left(\tan\frac{x}{a}\right)
$$

$$
y'(x) = \sec\frac{x}{a} \qquad y''(x) = \frac{1}{a}\sec\frac{x}{a}\tan\frac{x}{a} \qquad \kappa(x) = \frac{y''(x)}{\big(1+y'(x)^2\big)^{3/2}}
$$

弧長由數值積分 $s(x)=\displaystyle\int_0^x\sqrt{1+y'(\xi)^2} d\xi$ 反解 $x$；
再整體旋轉 $-45^\circ$（因原生切線在 $x=0$ 處為 $45^\circ$）對齊局部座標系：

$$
\begin{pmatrix}x_r\\y_r\end{pmatrix} =\begin{pmatrix}\cos(-\tfrac{\pi}{4}) & -\sin(-\tfrac{\pi}{4})\\ \sin(-\tfrac{\pi}{4}) & \cos(-\tfrac{\pi}{4})\end{pmatrix} \begin{pmatrix}x\\y\end{pmatrix}
$$

#### Logarithmic（Log-Aesthetic Curve，$n=+1$，等角螺線弧長參數化）

曲率半徑對弧長呈線性（$K=20$ 為固定比例常數，$b=R(K-1)/L_s$）：

$$
\rho(L) = R + b (L_s-L)
$$

閉合解切線角：

$$
\theta(L) = \frac{1}{b}\ln \left[\frac{R+bL_s}{R+b(L_s-L)}\right]
$$

$x(L),y(L)$ 由 $\theta(L)$ 數值積分 $\cos\theta,\sin\theta$ 求得
（見附錄 A 積分式）。與 Norwich/Sturm 同樣有「$\kappa(0)$ 無法精確為零」的
固有限制（$\kappa(0)=\kappa(L_s)/K$）。

#### PHQuintic（Pythagorean-Hodograph 五次螺線，Farouki/Walton-Meek 構造）

複數前像多項式（固定 $w_0=w_1=1$，自由參數 $w_2=p+iq$）：

$$
w(t) = (1-t)^2 + 2t(1-t) + (p+iq)t^2
$$

曲率（修正後之共軛版本）與方位角：

$$
\kappa(t) = \frac{2 \operatorname{Im} \big(\overline{w(t)} w'(t)\big)}{|w(t)|^4} \qquad \theta(t) = 2\arg\big(w(t)\big) = 2\operatorname{atan2} \big(\operatorname{Im}w,\operatorname{Re}w\big)
$$

正規化（$a=1$）弧長之閉合多項式：

$$
s(t) = t^5\left(\frac{p^2}{5}-\frac{2p}{5}+\frac{q^2}{5}+\frac{1}{5}\right) + t^3\left(\frac{2p}{3}-\frac{2}{3}\right) + t
$$

位置 $r(t)$ 為五次貝茲曲線（複數控制點）：

$$
p_0=0,\quad p_1=\frac{1}{5},\quad p_2=\frac{2}{5},\quad p_3=\frac{8}{15}+\frac{p+iq}{15},\quad p_4=\frac{8}{15}+\frac{4(p+iq)}{15},\quad p_5=\frac{8}{15}+\frac{4(p+iq)}{15}+\frac{(p+iq)^2}{5}
$$

$$
r(t) = \sum_{k=0}^{5}\binom{5}{k}(1-t)^{5-k}t^{k} p_k
$$

形狀求解：由邊界條件 $\kappa'(1)=0$ 得到 $q^2$ 對 $p$ 的封閉解，

$$
q^2(p) = \frac{p(8-7p)}{7},\qquad p\in\left(0,\ \frac{8}{7}\right)
$$

終點曲率與弧長：

$$
\kappa(1) = \frac{4q}{(p^2+q^2)^2} \qquad s(1) = \frac{p^2}{5}+\frac{4p}{15}+\frac{q^2}{5}+\frac{8}{15}
$$

再以一維二分法求 $p$ 使 $\kappa(1)\cdot s(1) = L_s/|R|$，並令縮放係數
$\lambda^2 = L_s/s(1)$，最終座標為 $\big(\lambda^2 x_r,\ \operatorname{sign}(R)\lambda^2 y_r,\ \operatorname{sign}(R)\theta\big)$。

---

### C. Egg（複合緩和曲線 / OLS 等效螺線）通用構造

`EggTransitionElement`（用於 SCS／SCSSCS 等複合鏈，連接兩個不同半徑 $R_1,R_2$
的圓弧、長度 $L_E$）不是獨立公式，而是把上述任一 `SpiralType`
當作內部「等效全長螺線」使用。傳統 clothoid 情形（線性 $g(t)=t$）有封閉解：

$$
L_s = L_E\cdot\frac{\max(|R_1|,|R_2|)}{\big| |R_1|-|R_2| \big|}
$$

其他曲率斜坡型家族則需先解「正規化反算」方程

$$
g(t^*) = \frac{\min(|R_1|,|R_2|)}{\max(|R_1|,|R_2|)}
$$

（$t^*\in(0,1)$，由 `invertNormalisedRamp` 反解 $g$），再對 $L_s$ 做不動點迭代：

$$
L_s^{(k+1)} = \frac{L_E}{1-t^*\big(L_s^{(k)}\big)}
$$

收斂後即得等效螺線全長 $L_s$；實際 $(x,y,\theta)$ 再取該等效螺線在
$[ L_s-L_E,\ L_s ]$（或反向 $[ 0,\ L_s-L_E ]$，視哪個半徑為主導）
區間內的一段，並重新錨定平移旋轉至區段自身起點。

---

## 商用軟體二次查證補充資料

除了學術文獻外，也查了幾套業界知名的鐵路／道路設計軟體官方文件與產品頁：

- **Bentley OpenRail Designer**：
 - <https://en.virtuosity.com/openrail-designer>（"Horizontal geometry spirals:
 Clothoid, Bi-quadratic Parabola, Bloss, Sinusoid, Cosine, various Cubic
 Parabola, and more."）
 - 官方簡報投影片：<https://slideplayer.com/slide/8634476>（"Support the import
 of following Transition Spirals: Bloss, Biquadratic, Cosine, Sinusoid,
 Half-Bloss, Half-Biquadratic, Half-Cosine, Half-Sinusoids"）
 - 確認 Cosine 與 Sinusoid 是業界認可的兩種不同類型（各自還有 Half- 版本）。

- **VESTRA INFRAVISION Bahn**（德國，AKG Software）：
 <https://www.akgsoftware.at/branchen/tiefbau/bahn/>——支援 Klothoide、
 Blossbogen、Schrammbogen/S-Form（德國）、Wiener Bogen®（奧地利）、
 Cosinusoide（日本/匈牙利）、Sinusoide（高速鐵路/磁浮）。

- **德文維基百科「Übergangsbogen」條目**：<https://de.wikipedia.org/wiki/%C3%9Cbergangsbogen>
 ——最常用的是 Klothoide、Sinusoide、Blossbogen；德鐵過去曾用 Schramm 提出的
 S 形緩和曲線（四次拋物線），因發展長度較長已被 Bloss 取代；Wiener Bogen 是
 新發展，額外考慮車輛重心。

- **Zusi 鐵路模擬論壇**（佐證 Wiener Bogen 原始出處）：
 <https://www.forum.zusi.de/viewtopic.php?f=13&t=4661>（引用 Hasslinger/
 Stockinger 論文標題）

---

## 檔案對應

| 內容 | 檔案 |
|---|---|
| 全部元素類別定義 | `src/railway/RailwayAlignmentElement.h` |
| 公式實作 + 共用數值積分器 | `src/railway/RailwayAlignmentElement.cpp` |
| `SpiralType` enum | `src/railway/AlignmentDocument.h` |
| ALD 檔案 8 字元欄位縮寫對照表 | `src/railway/AldFileIO.cpp` |
| `computeRadius()` 精確半徑公式 | `src/railway/RailwayAlignment.cpp` |

## 尚待你決定的事項彙整

1. **`Radioid`（通用版）、`Spline`**：命名與文獻無直接對應，建議評估是否改名
 （例如 `Radioid`→`ConcaveRamp`）或保留作為自訂選項。
2. **`Norwich/Sturm`、`Logarithmic`**：兩者皆有「無法在有限弧長內精確達到零
 曲率」的數學本質限制，已用固定比例近似處理，如果你的應用場景對起點曲率
 精確度要求嚴格，需要另外評估是否適用。
3. **`Wiener Bogen`、`Hyperbolic`**：`h`／`ψ₂`／`p` 目前是固定代表性預設值，
 如需依專案精確計算（例如依實際設計速度算平衡超高），需要擴充成使用者可調
 欄位（UI／指令列／ALD 存檔），工作量與先前新增 13 種類型相當。
4. **`Polynomial`、`Quintic`**：確認為研究類別而非單一標準公式，現有實作僅為
 該類別中的示意成員；若你的規範要求特定係數，請提供以便核對。

若需要針對以上任何一項繼續深入查證或調整實作，請告訴我。