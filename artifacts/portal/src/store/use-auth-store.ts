import { create } from 'zustand';
import { SpielerProfile } from '@workspace/api-client-react';

interface AuthState {
  token: string | null;
  user: SpielerProfile | null;
  setAuth: (user: SpielerProfile, token: string) => void;
  logout: () => void;
  isAuthenticated: boolean;
}

export const useAuthStore = create<AuthState>((set) => {
  const token = typeof localStorage !== 'undefined' ? localStorage.getItem('rangemaster-token') : null;
  const userString = typeof localStorage !== 'undefined' ? localStorage.getItem('rangemaster-user') : null;
  let user = null;
  if (userString) {
    try {
      user = JSON.parse(userString);
    } catch(e) {}
  }
  
  return {
    token,
    user,
    isAuthenticated: !!token,
    setAuth: (user, token) => {
      localStorage.setItem('rangemaster-token', token);
      localStorage.setItem('rangemaster-user', JSON.stringify(user));
      set({ user, token, isAuthenticated: true });
    },
    logout: () => {
      localStorage.removeItem('rangemaster-token');
      localStorage.removeItem('rangemaster-user');
      set({ user: null, token: null, isAuthenticated: false });
    }
  };
});
