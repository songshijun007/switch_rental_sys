// Key generation utilities shared between the web key generator and the
// Switch-side rental lock system.
//
// The algorithm is intentionally simple so it can be re-implemented in pure C
// for the Nintendo Switch homebrew (where only basic libc + a tiny SHA-256
// implementation is conveniently available):
//
//   periodIndex = floor((unixTimestamp - epoch) / periodDays / 86400)
//   mac         = HMAC-SHA256(saltUtf8, machineCodeUtf8 + ":" + periodIndexAscii)
//   unlockKey   = 8 decimal digits (first 8 decimal chars from hex-mac)
//
// The Switch stores the highest period index that has been paid for. When the
// current period exceeds the paid period (plus a small grace), the console is
// locked via the parental-control play-timer being set to zero.

const ENCODER = new TextEncoder();

function hexToDecimal(hex: string): string {
  // Convert the first 8 hex nibbles to a decimal integer zero-padded to 8
  // digits. 8 hex nibbles = 32 bits → max 4,294,967,295 which is 10 decimal
  // digits, but we take the first 8 so we always have a fixed 8-digit code.
  const v = parseInt(hex.substring(0, 8), 16);
  return (v % 100000000).toString().padStart(8, "0");
}

async function hmacSha256Hex(key: string, message: string): Promise<string> {
  const cryptoKey = await crypto.subtle.importKey(
    "raw",
    ENCODER.encode(key),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );
  const sig = await crypto.subtle.sign("HMAC", cryptoKey, ENCODER.encode(message));
  return Array.from(new Uint8Array(sig))
    .map((b) => b.toString(16).padStart(2, "0"))
    .join("");
}

/**
 * An arbitrary epoch picked so that period indexes start small.
 * 2024-01-01 00:00:00 UTC.
 */
export const RENTAL_EPOCH = 1704067200;

export interface KeyParams {
  salt: string;
  machineCode: string;
  periodDays: number;
  /** Unix timestamp in seconds for which to generate the key (defaults to now). */
  timestampSec?: number;
}

export interface KeyResult {
  /** Period index used. */
  period: number;
  /** Start of the period (unix seconds). */
  periodStart: number;
  /** End of the period (unix seconds, inclusive). */
  periodEnd: number;
  /** The 8-digit unlock key. */
  key: string;
  /** The full HMAC hex (for debugging). */
  macHex: string;
}

export function computePeriod(
  timestampSec: number,
  periodDays: number,
): { period: number; periodStart: number; periodEnd: number } {
  const periodLen = periodDays * 86400;
  const period = Math.floor((timestampSec - RENTAL_EPOCH) / periodLen);
  const periodStart = RENTAL_EPOCH + period * periodLen;
  const periodEnd = periodStart + periodLen - 1;
  return { period, periodStart, periodEnd };
}

export async function generateKey({
  salt,
  machineCode,
  periodDays,
  timestampSec,
}: KeyParams): Promise<KeyResult> {
  const ts = timestampSec ?? Math.floor(Date.now() / 1000);
  const { period, periodStart, periodEnd } = computePeriod(ts, periodDays);
  const msg = `${normalizeMachineCode(machineCode)}:${period}`;
  const macHex = await hmacSha256Hex(salt, msg);
  const key = hexToDecimal(macHex);
  return { period, periodStart, periodEnd, key, macHex };
}

export interface BatchKeyParams {
  salt: string;
  machineCode: string;
  periodDays: number;
  /** Number of consecutive periods to generate starting at current period. */
  count: number;
  timestampSec?: number;
}

export async function generateKeyBatch({
  salt,
  machineCode,
  periodDays,
  count,
  timestampSec,
}: BatchKeyParams): Promise<KeyResult[]> {
  const ts = timestampSec ?? Math.floor(Date.now() / 1000);
  const { period } = computePeriod(ts, periodDays);
  const out: KeyResult[] = [];
  for (let i = 0; i < count; i++) {
    const targetPeriodStart = RENTAL_EPOCH + (period + i) * periodDays * 86400;
    out.push(
      await generateKey({
        salt,
        machineCode,
        periodDays,
        timestampSec: targetPeriodStart,
      }),
    );
  }
  return out;
}

/**
 * Validate a user-entered key against a window of periods around "now", and
 * return the matching period index if it matches. This mirrors what the Switch
 * does when the renter types in an unlock code.
 */
export async function matchKey(
  salt: string,
  machineCode: string,
  periodDays: number,
  enteredKey: string,
  lookAheadPeriods = 52 * 4, // ~4 years of weeks
): Promise<{ matched: true; period: number } | { matched: false }> {
  const ts = Math.floor(Date.now() / 1000);
  const { period } = computePeriod(ts, periodDays);
  const normKey = enteredKey.replace(/\s+/g, "").trim();
  for (let i = 0; i < lookAheadPeriods; i++) {
    const p = period + i;
    const msg = `${normalizeMachineCode(machineCode)}:${p}`;
    const macHex = await hmacSha256Hex(salt, msg);
    const candidate = hexToDecimal(macHex);
    if (candidate === normKey) {
      return { matched: true, period: p };
    }
  }
  return { matched: false };
}

export function normalizeMachineCode(code: string): string {
  // Machine codes on the Switch are shown as hex; accept any combination of
  // upper/lower case and optional separators.
  return code.replace(/[^0-9a-fA-F]/g, "").toUpperCase();
}

/**
 * Format a unix seconds timestamp as a human readable date (in local tz).
 */
export function formatDate(sec: number): string {
  return new Date(sec * 1000).toLocaleString();
}
