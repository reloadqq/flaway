import { useEffect } from "react";

export default function useParallax(enabled) {
  useEffect(() => {
    if (!enabled) {
      document.documentElement.style.setProperty("--mx", "50%");
      document.documentElement.style.setProperty("--my", "50%");
      return undefined;
    }

    let mouseX = 50;
    let mouseY = 50;
    let currentX = 50;
    let currentY = 50;
    let frame = 0;

    const tick = () => {
      frame = 0;

      currentX += (mouseX - currentX) * 0.08;
      currentY += (mouseY - currentY) * 0.08;

      const root = document.documentElement;
      root.style.setProperty("--mx", currentX + "%");
      root.style.setProperty("--my", currentY + "%");

      if (
        Math.abs(mouseX - currentX) > 0.05 ||
        Math.abs(mouseY - currentY) > 0.05
      ) {
        frame = requestAnimationFrame(tick);
      }
    };

    const onMove = (event) => {
      mouseX = (event.clientX / window.innerWidth) * 100;
      mouseY = (event.clientY / window.innerHeight) * 100;

      if (!frame) frame = requestAnimationFrame(tick);
    };

    document.addEventListener("mousemove", onMove, { passive: true });

    return () => {
      document.removeEventListener("mousemove", onMove);
      if (frame) cancelAnimationFrame(frame);
    };
  }, [enabled]);
}
