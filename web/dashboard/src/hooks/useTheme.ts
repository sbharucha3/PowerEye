import { useCallback, useState } from "react";

export function useTheme() {
  const [dark, setDark] = useState(() => document.documentElement.classList.contains("dark"));
  const toggle = useCallback(() => {
    setDark((d) => {
      const next = !d;
      document.documentElement.classList.toggle("dark", next);
      try {
        localStorage.setItem("pe-theme", next ? "dark" : "light");
      } catch {
        /* storage unavailable */
      }
      return next;
    });
  }, []);
  return { dark, toggle };
}
