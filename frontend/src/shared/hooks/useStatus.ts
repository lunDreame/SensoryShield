import { useEffect, useState } from "react";
import { fallbackStatus, getStatus } from "../api/client";
import type { StatusResponse } from "../types/domain";

export function useStatus() {
  const [status, setStatus] = useState<StatusResponse>(fallbackStatus);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let active = true;

    async function poll() {
      try {
        const next = await getStatus();
        if (active) {
          setStatus(next);
          setError(null);
        }
      } catch (err) {
        if (active) {
          setError(err instanceof Error ? err.message : "status unavailable");
        }
      }
    }

    poll();
    const timer = window.setInterval(poll, 1500);
    return () => {
      active = false;
      window.clearInterval(timer);
    };
  }, []);

  return { status, error };
}
