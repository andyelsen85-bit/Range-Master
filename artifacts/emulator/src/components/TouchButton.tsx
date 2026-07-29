import React from 'react';
import { cn } from '@/lib/utils';

interface TouchButtonProps extends React.ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: 'default' | 'primary' | 'destructive' | 'outline' | 'ghost' | 'success' | 'warning';
  size?: 'default' | 'lg' | 'xl';
  active?: boolean;
}

export function TouchButton({
  className,
  variant = 'default',
  size = 'default',
  active,
  ...props
}: TouchButtonProps) {
  return (
    <button
      className={cn(
        "inline-flex items-center justify-center rounded-md font-bold transition-colors select-none",
        "focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2",
        "disabled:opacity-50 disabled:pointer-events-none ring-offset-background",
        "active:scale-[0.98] active:brightness-110", // Touch feedback
        {
          'bg-card text-card-foreground border-2 border-border': variant === 'default',
          'bg-primary text-primary-foreground': variant === 'primary',
          'bg-destructive text-destructive-foreground': variant === 'destructive',
          'bg-[#4ade80] text-black': variant === 'success',
          'bg-[#fbbf24] text-black': variant === 'warning',
          'border-2 border-primary bg-transparent text-primary': variant === 'outline',
          'hover:bg-accent hover:text-accent-foreground': variant === 'ghost',
          
          'h-14 px-6 text-lg': size === 'default',
          'h-20 px-8 text-xl': size === 'lg',
          'h-24 px-10 text-2xl': size === 'xl',
          
          'ring-4 ring-primary ring-offset-background': active
        },
        className
      )}
      {...props}
    />
  );
}
