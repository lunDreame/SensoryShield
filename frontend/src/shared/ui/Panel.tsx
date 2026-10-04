import type { ReactNode } from "react";

interface PanelProps {
  title: string;
  subtitle?: string;
  action?: ReactNode;
  icon?: ReactNode;
  className?: string;
  children: ReactNode;
}

export function Panel({ title, subtitle, action, icon, className = "", children }: PanelProps) {
  return (
    <section className={`panel ${className}`.trim()}>
      <div className="panel-header">
        <div className="panel-title-group">{icon}<div><h2>{title}</h2>{subtitle ? <p className="panel-subtitle">{subtitle}</p> : null}</div></div>
        {action}
      </div>
      {children}
    </section>
  );
}
