"use client";

import { useMemo, useState } from "react";
import {
  computePeriod,
  formatDate,
  generateKey,
  generateKeyBatch,
  matchKey,
  normalizeMachineCode,
  RENTAL_EPOCH,
} from "@/lib/keygen";

type BatchRow = {
  period: number;
  periodStart: number;
  periodEnd: number;
  key: string;
};

export default function HomePage() {
  const [salt, setSalt] = useState("my-rental-secret-2025");
  const [machineCode, setMachineCode] = useState("");
  const [periodDays, setPeriodDays] = useState(7);
  const [batchCount, setBatchCount] = useState(4);
  const [oneKey, setOneKey] = useState("");
  const [onePeriod, setOnePeriod] = useState<number | null>(null);
  const [batch, setBatch] = useState<BatchRow[]>([]);
  const [verifyInput, setVerifyInput] = useState("");
  const [verifyResult, setVerifyResult] = useState<string>("");
  const [loading, setLoading] = useState(false);
  const [tab, setTab] = useState<"single" | "batch" | "verify" | "about">(
    "single",
  );

  const currentPeriodInfo = useMemo(() => {
    const ts = Math.floor(Date.now() / 1000);
    const p = computePeriod(ts, periodDays);
    return p;
  }, [periodDays]);

  async function handleGenerateOne() {
    if (!machineCode.trim()) {
      alert("请输入机器码");
      return;
    }
    setLoading(true);
    try {
      const r = await generateKey({
        salt,
        machineCode,
        periodDays,
      });
      setOneKey(r.key);
      setOnePeriod(r.period);
    } finally {
      setLoading(false);
    }
  }

  async function handleGenerateBatch() {
    if (!machineCode.trim()) {
      alert("请输入机器码");
      return;
    }
    setLoading(true);
    try {
      const rows = await generateKeyBatch({
        salt,
        machineCode,
        periodDays,
        count: batchCount,
      });
      setBatch(rows);
    } finally {
      setLoading(false);
    }
  }

  async function handleVerify() {
    if (!machineCode.trim()) {
      setVerifyResult("请先输入机器码");
      return;
    }
    const m = await matchKey(salt, machineCode, periodDays, verifyInput);
    if (m.matched) {
      const start =
        RENTAL_EPOCH + m.period * periodDays * 86400;
      const end = start + periodDays * 86400 - 1;
      setVerifyResult(
        `秘钥有效，对应周期 #${m.period}（${formatDate(start)} ～ ${formatDate(end)}）。输入此秘钥后，机器将解锁至该周期结束。`,
      );
    } else {
      setVerifyResult("秘钥无效：与当前或未来周期不匹配。请检查盐值、机器码、周期天数是否一致。");
    }
  }

  const normalizedMC = machineCode
    ? normalizeMachineCode(machineCode)
    : "";

  return (
    <main className="min-h-screen bg-gradient-to-br from-slate-900 via-slate-800 to-slate-900 px-4 py-10 text-slate-100">
      <div className="mx-auto w-full max-w-4xl">
        <header className="mb-8 text-center">
          <p className="text-xs uppercase tracking-[0.25em] text-emerald-400">
            Switch Rental Lock · 出租机离线密钥生成器
          </p>
          <h1 className="mt-3 text-4xl font-bold tracking-tight">
            PCTL All-in-One · 出租版
          </h1>
          <p className="mx-auto mt-3 max-w-2xl text-slate-300">
            基于机器码 + 周期 + 盐值生成 8 位数字解锁秘钥，完全离线校验，
            租客在 Switch 上输入后即可续期，未续期则通过家长控制系统锁定机器。
          </p>
        </header>

        <section className="mb-6 rounded-2xl bg-slate-800/70 p-6 shadow-xl ring-1 ring-white/5">
          <h2 className="mb-4 text-lg font-semibold text-emerald-300">
            全局参数（机主持有，请勿泄露给租客）
          </h2>
          <div className="grid gap-4 md:grid-cols-3">
            <label className="block">
              <span className="mb-1 block text-sm text-slate-300">
                盐值 Salt（自定义，越复杂越安全）
              </span>
              <input
                className="w-full rounded-lg border border-slate-600 bg-slate-900/70 px-3 py-2 font-mono text-sm focus:border-emerald-400 focus:outline-none"
                value={salt}
                onChange={(e) => setSalt(e.target.value)}
                placeholder="your-secret-salt"
              />
            </label>
            <label className="block">
              <span className="mb-1 block text-sm text-slate-300">
                出租周期（天）
              </span>
              <input
                type="number"
                min={1}
                max={365}
                className="w-full rounded-lg border border-slate-600 bg-slate-900/70 px-3 py-2 font-mono text-sm focus:border-emerald-400 focus:outline-none"
                value={periodDays}
                onChange={(e) => setPeriodDays(Math.max(1, Number(e.target.value) || 1))}
              />
            </label>
            <label className="block">
              <span className="mb-1 block text-sm text-slate-300">
                当前周期
              </span>
              <div className="rounded-lg border border-slate-700 bg-slate-900/40 px-3 py-2 font-mono text-sm text-emerald-300">
                #{currentPeriodInfo.period} · 截止{" "}
                {formatDate(currentPeriodInfo.periodEnd)}
              </div>
            </label>
          </div>

          <div className="mt-4">
            <label className="block">
              <span className="mb-1 block text-sm text-slate-300">
                机器码（从 Switch 锁屏页面 / Web 界面读取，16 位十六进制）
              </span>
              <input
                className="w-full rounded-lg border border-slate-600 bg-slate-900/70 px-3 py-2 font-mono text-sm uppercase tracking-wider focus:border-emerald-400 focus:outline-none"
                value={machineCode}
                onChange={(e) => setMachineCode(e.target.value)}
                placeholder="例如：4000A1B2C3D4E5F6（16 位十六进制）"
              />
              {normalizedMC && (
                <p className="mt-1 text-xs text-slate-400">
                  规范化后：
                  <span className="ml-1 font-mono text-emerald-300">
                    {normalizedMC}
                  </span>
                </p>
              )}
            </label>
          </div>
        </section>

        <nav className="mb-6 flex flex-wrap gap-2">
          {([
            ["single", "当前周期秘钥"],
            ["batch", "批量生成 N 个周期"],
            ["verify", "验证租客秘钥"],
            ["about", "集成说明"],
          ] as const).map(([key, label]) => (
            <button
              key={key}
              onClick={() => setTab(key)}
              className={
                "rounded-lg px-4 py-2 text-sm font-medium transition " +
                (tab === key
                  ? "bg-emerald-500 text-slate-950 shadow-lg shadow-emerald-500/20"
                  : "bg-slate-800 text-slate-300 hover:bg-slate-700")
              }
            >
              {label}
            </button>
          ))}
        </nav>

        {tab === "single" && (
          <section className="rounded-2xl bg-slate-800/70 p-6 shadow-xl ring-1 ring-white/5">
            <h2 className="mb-2 text-lg font-semibold text-emerald-300">
              生成当前周期的解锁秘钥
            </h2>
            <p className="mb-4 text-sm text-slate-400">
              给租客发送该秘钥，可解锁至当前周期结束（机器端还会接受未来周期秘钥，
              用于提前支付下一周期）。
            </p>
            <button
              onClick={handleGenerateOne}
              disabled={loading}
              className="rounded-lg bg-emerald-500 px-5 py-2 font-semibold text-slate-950 hover:bg-emerald-400 disabled:opacity-50"
            >
              {loading ? "生成中..." : "生成当前周期秘钥"}
            </button>
            {oneKey && (
              <div className="mt-6 rounded-xl bg-slate-950/60 p-5 ring-1 ring-emerald-500/30">
                <div className="text-xs text-slate-400">当前周期 #{onePeriod} 解锁秘钥</div>
                <div className="mt-1 select-all font-mono text-5xl font-bold tracking-[0.2em] text-emerald-300">
                  {oneKey}
                </div>
                <p className="mt-3 text-xs text-slate-400">
                  租客在 Switch 的锁屏提示处输入此 8 位数字即可解锁。
                </p>
              </div>
            )}
          </section>
        )}

        {tab === "batch" && (
          <section className="rounded-2xl bg-slate-800/70 p-6 shadow-xl ring-1 ring-white/5">
            <h2 className="mb-2 text-lg font-semibold text-emerald-300">
              批量生成多个连续周期的秘钥
            </h2>
            <p className="mb-4 text-sm text-slate-400">
              租客一次性支付多个周期租金时，可把这些秘钥按顺序发送，也可直接发送最后一个周期的秘钥（机器端会自动匹配到对应周期）。
            </p>
            <div className="flex flex-wrap items-end gap-3">
              <label className="block">
                <span className="mb-1 block text-sm text-slate-300">
                  生成数量（周期数）
                </span>
                <input
                  type="number"
                  min={1}
                  max={200}
                  className="w-32 rounded-lg border border-slate-600 bg-slate-900/70 px-3 py-2 font-mono text-sm"
                  value={batchCount}
                  onChange={(e) =>
                    setBatchCount(Math.max(1, Number(e.target.value) || 1))
                  }
                />
              </label>
              <button
                onClick={handleGenerateBatch}
                disabled={loading}
                className="rounded-lg bg-emerald-500 px-5 py-2 font-semibold text-slate-950 hover:bg-emerald-400 disabled:opacity-50"
              >
                {loading ? "生成中..." : "批量生成"}
              </button>
            </div>
            {batch.length > 0 && (
              <div className="mt-6 overflow-hidden rounded-xl ring-1 ring-white/10">
                <table className="w-full text-sm">
                  <thead className="bg-slate-900/80 text-slate-300">
                    <tr>
                      <th className="px-4 py-2 text-left">#</th>
                      <th className="px-4 py-2 text-left">起始时间</th>
                      <th className="px-4 py-2 text-left">截止时间</th>
                      <th className="px-4 py-2 text-right">秘钥</th>
                    </tr>
                  </thead>
                  <tbody>
                    {batch.map((r, i) => (
                      <tr
                        key={r.period}
                        className={
                          "border-t border-white/5 font-mono " +
                          (i === 0
                            ? "bg-emerald-500/10 text-emerald-200"
                            : "text-slate-200")
                        }
                      >
                        <td className="px-4 py-2">{r.period}</td>
                        <td className="px-4 py-2">{formatDate(r.periodStart)}</td>
                        <td className="px-4 py-2">{formatDate(r.periodEnd)}</td>
                        <td className="px-4 py-2 text-right text-2xl font-bold tracking-widest">
                          {r.key}
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
                <div className="bg-slate-950/50 px-4 py-2 text-xs text-slate-400">
                  提示：机器端只需要输入最后一个周期的秘钥即可直接解锁到该周期结束；
                  也可以依次输入每个周期秘钥，逐期续费。
                </div>
              </div>
            )}
          </section>
        )}

        {tab === "verify" && (
          <section className="rounded-2xl bg-slate-800/70 p-6 shadow-xl ring-1 ring-white/5">
            <h2 className="mb-2 text-lg font-semibold text-emerald-300">
              验证秘钥
            </h2>
            <p className="mb-4 text-sm text-slate-400">
              输入租客提供的秘钥，检查是否与当前参数匹配，并查看对应周期。
            </p>
            <div className="flex flex-wrap gap-3">
              <input
                className="flex-1 rounded-lg border border-slate-600 bg-slate-900/70 px-3 py-2 font-mono text-2xl tracking-[0.2em] focus:border-emerald-400 focus:outline-none"
                maxLength={8}
                value={verifyInput}
                onChange={(e) =>
                  setVerifyInput(e.target.value.replace(/\D/g, "").slice(0, 8))
                }
                placeholder="8 位数字秘钥"
              />
              <button
                onClick={handleVerify}
                className="rounded-lg bg-emerald-500 px-5 py-2 font-semibold text-slate-950 hover:bg-emerald-400"
              >
                验证
              </button>
            </div>
            {verifyResult && (
              <div
                className={
                  "mt-4 rounded-lg px-4 py-3 text-sm " +
                  (verifyResult.startsWith("秘钥有效")
                    ? "bg-emerald-500/10 text-emerald-200 ring-1 ring-emerald-500/40"
                    : "bg-rose-500/10 text-rose-200 ring-1 ring-rose-500/40")
                }
              >
                {verifyResult}
              </div>
            )}
          </section>
        )}

        {tab === "about" && (
          <section className="space-y-4 rounded-2xl bg-slate-800/70 p-6 text-slate-300 shadow-xl ring-1 ring-white/5">
            <h2 className="text-lg font-semibold text-emerald-300">
              工作原理（出租方必读）
            </h2>
            <ol className="list-decimal space-y-2 pl-5">
              <li>
                刷机后将编译好的 <code className="rounded bg-slate-900 px-1">pctltcp-allinone.nro</code>
                {"  "}放到 SD 卡 <code className="rounded bg-slate-900 px-1">/switch/</code> 目录，
                首次运行时会在 SD 卡生成配置，并读取 Switch 设备 ID 作为机器码。
              </li>
              <li>
                盐值和出租周期在编译时写入 NRO（见 <code>switch-src/source/config.h</code>），
                也可以通过 SD 卡 <code className="rounded bg-slate-900 px-1">/switch/pctltcp/rental.ini</code> 覆盖。
                请务必在第一次交付租客前设置好自己的盐值，否则任何人都能算出秘钥。
              </li>
              <li>
                每次启动 app 都会计算当前周期，若当前周期超过已解锁周期，立即调用
                Nintendo 官方的家长控制调试接口把每日可玩时间设为 <b>0 分钟</b>，
                机器进入锁定状态，无法启动游戏。
              </li>
              <li>
                锁定状态下，Switch 屏幕和 Web 管理页（http://Switch-IP:8081）会显示机器码和解锁输入框。
                租客把机器码发给机主，机主在本页面生成 8 位数字秘钥发回。
              </li>
              <li>
                租客在 Switch 上输入秘钥，NRO 在本地用相同算法验证，验证通过后把解锁周期写入 SD 卡并重置家长控制。
                整个过程 <b>完全离线</b>，不需要服务器或联网验证。
              </li>
            </ol>
            <h2 className="mt-6 text-lg font-semibold text-emerald-300">
              交付流程
            </h2>
            <ul className="list-disc space-y-2 pl-5">
              <li>机主在 GitHub 上 Fork 本仓库，修改 <code>config.h</code> 中的 <code>DEFAULT_SALT</code> 为自定义字符串。</li>
              <li>推送代码到 GitHub，Actions 自动编译 NRO，并在 Releases 页面提供下载。</li>
              <li>将 NRO 放到租客的 SD 卡，启动一次获取机器码；或者把机器交给租客，让租客截图机器码发回。</li>
              <li>每次收租后，机主在本网站用相同盐值 + 机器码生成新秘钥发给租客。</li>
            </ul>
            <h2 className="mt-6 text-lg font-semibold text-emerald-300">
              安全性说明
            </h2>
            <ul className="list-disc space-y-2 pl-5 text-sm text-slate-400">
              <li>算法基于 HMAC-SHA256，单向不可逆。只要盐值不泄露，即使租客拿到自己的机器码和秘钥，也无法为其他机器或其他周期生成秘钥。</li>
              <li>秘钥仅为 8 位数字（1 亿种可能），暴力破解在离线场景下不现实——Switch 端对错误输入有冷却时间和计数限制。</li>
              <li>解锁信息写入 SD 卡，若租客删除配置文件，NRO 下次启动会视为首次运行并立即锁定。</li>
              <li>锁定通过 Nintendo 官方的 <code>SetPlayTimerSettingsForDebug</code> 服务实现，重启后依然有效。</li>
            </ul>
          </section>
        )}

        <footer className="mt-10 text-center text-xs text-slate-500">
          <p>
            Switch 端源码位于 <code>switch-src/</code>；GitHub Actions 自动构建 NRO 并发布到 Releases。
          </p>
          <p className="mt-1">本页面密钥生成纯浏览器端计算，不上传任何数据。</p>
        </footer>
      </div>
    </main>
  );
}
