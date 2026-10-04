import type { ReactNode } from "react";

interface IconProps {
  className?: string;
}

function IconFrame({ children, className }: IconProps & { children: ReactNode }) {
  return <svg className={className} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">{children}</svg>;
}

export function ChildViewIcon(props: IconProps) {
  return <IconFrame {...props}><circle cx="12" cy="12" r="8" /><path d="M9 10h.01M15 10h.01M8.8 14.2c.9 1.1 2 1.6 3.2 1.6s2.3-.5 3.2-1.6" /></IconFrame>;
}

export function GuardianViewIcon(props: IconProps) {
  return <IconFrame {...props}><path d="M12 3 5.5 5.8v4.8c0 4.3 2.6 7.9 6.5 10 3.9-2.1 6.5-5.7 6.5-10V5.8L12 3Z" /><circle cx="12" cy="9.5" r="2" /><path d="M8.8 15c.8-1.5 1.9-2.2 3.2-2.2s2.4.7 3.2 2.2" /></IconFrame>;
}

export function SoftLightIcon(props: IconProps) {
  return <IconFrame {...props}><circle cx="10.5" cy="10.5" r="3.5" /><path d="M10.5 3v2M10.5 16v2M3 10.5h2M16 10.5h2M5.2 5.2l1.4 1.4M14.4 14.4l1.4 1.4M15.8 5.2l-1.4 1.4M5.2 15.8l1.4-1.4M16.5 19.5h4M18.5 17.5v4" /></IconFrame>;
}

export function QuietSoundIcon(props: IconProps) {
  return <IconFrame {...props}><path d="M5 14h3l4 3V7l-4 3H5v4Z" /><path d="M15.2 10.2c.8 1 .8 2.6 0 3.6M18 8.2c1.8 2.1 1.8 5.5 0 7.6" /><path d="m5 19 14-14" /></IconFrame>;
}

export function ComfortableIcon(props: IconProps) {
  return <IconFrame {...props}><path d="M12 20s-7-4.1-7-10a4 4 0 0 1 7-2.6A4 4 0 0 1 19 10c0 5.9-7 10-7 10Z" /><path d="m9.5 12.2 1.7 1.7 3.5-3.7" /></IconFrame>;
}

export function CalmHomeIcon(props: IconProps) {
  return <IconFrame {...props}><path d="m4 11 8-6 8 6v8H4v-8Z" /><path d="M9 19v-5h6v5M8 10.5h.01M16 10.5h.01" /><path d="M10 12.2c.6.6 1.3.9 2 .9s1.4-.3 2-.9" /></IconFrame>;
}
