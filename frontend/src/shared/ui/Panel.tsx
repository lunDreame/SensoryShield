import type { ReactNode } from "react";

interface PanelProps {
  title: string;
  subtitle?: string;
  action?: ReactNode;
  className?: string;
  children: ReactNode;
}

export function Panel({ title, subtitle, action, className = "", children }: PanelProps) {
  return (
    <section className={`panel ${className}`.trim()}>
      <div className="panel-header">
        <div><h2>{title}</h2>{subtitle ? <p className="panel-subtitle">{subtitle}</p> : null}</div>
        {action}
      </div>
      {children}
    </section>
  );
}
