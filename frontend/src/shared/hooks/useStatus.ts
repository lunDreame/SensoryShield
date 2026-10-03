import { useEffect, useState } from "react";
import { fallbackStatus, getStatus } from "../api/client";
import type { StatusResponse } from "../types/domain";

export function useStatus() {
  const [status, setStatus] = useState<StatusResponse>(fallbackStatus);
  const [error, setError] = useState<string | null>(null);
  const [updatedAt, setUpdatedAt] = useState<number | null>(null);

  useEffect(() => {
    let active = true;
    let timer: number | undefined;

    async function poll() {
      try {
        const next = await getStatus();
        if (active) {
          setStatus(next);
          setError(null);
          setUpdatedAt(Date.now());
        }
      } catch (err) {
        if (active) {
          setError(err instanceof Error ? err.message : "status unavailable");
          setStatus((current) => ({ ...current, system: { ...current.system, ready: false } }));
        }
      } finally {
        if (active) timer = window.setTimeout(poll, 1500);
      }
    }

    poll();
    return () => {
      active = false;
      window.clearTimeout(timer);
    };
  }, []);

  return { status, error, updatedAt };
}
