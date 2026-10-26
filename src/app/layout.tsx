import type { Metadata } from "next";
import type { ReactNode } from "react";
import "./globals.css";

export const metadata: Metadata = {
  title: "Switch 出租机秘钥生成器 · PCTL All-in-One Rental",
  description:
    "基于机器码和 HMAC-SHA256 的离线出租机解锁秘钥生成器，配合 Switch 家长控制 NRO 使用。",
};

export default function RootLayout({ children }: { children: ReactNode }) {
  return (
    <html lang="zh-CN">
      <body className="bg-slate-100 text-slate-900 antialiased">{children}</body>
    </html>
  );
}
