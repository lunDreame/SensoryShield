import { useEffect, useState } from "react";
import { initialStatus, getStatus } from "../api/client";
import type { StatusResponse } from "../types/domain";

export function useStatus() {
  const [status, setStatus] = useState<StatusResponse>(initialStatus);
  const [error, setError] = useState<string | null>(null);
  const [updatedAt, setUpdatedAt] = useState<number | null>(null);

  useEffect(() => {
    let active = true;
    let timer: number | undefined;
    let consecutiveFailures = 0;

    async function poll() {
      try {
        const next = await getStatus();
        if (active) {
          consecutiveFailures = 0;
          setStatus(next);
          setError(null);
          setUpdatedAt(Date.now());
        }
      } catch (err) {
        if (active) {
          consecutiveFailures += 1;
          if (consecutiveFailures >= 3) {
            setError(err instanceof Error ? err.message : "status unavailable");
            setStatus((current) => ({ ...current, system: { ...current.system, ready: false } }));
          }
        }
      } finally {
        if (active) timer = window.setTimeout(poll, 3000);
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
