import Script from 'next/script';
import Nav from '@/components/layout/Nav';
import Footer from '@/components/layout/Footer';
import PcbGrid from '@/components/layout/PcbGrid';
import './globals.css';

const GA_ID = 'G-KLZ2K0N2FT';

export const metadata = {
  title: 'UniGeek — Multi-tool firmware for ESP32',
  description:
    'Open-source multi-tool firmware for ESP32-family boards. WiFi, BLE, NFC, IR, Sub-GHz, USB HID — one reflashable image across twelve boards.',
  icons: {
    icon: '/favicon.svg',
    shortcut: '/favicon.svg',
    apple: '/apple-icon.svg',
  },
};

export const viewport = {
  themeColor: '#0b0d0e',
  width: 'device-width',
  initialScale: 1,
};

export default function RootLayout({ children }) {
  return (
    <html lang="en">
      <head>
        <link rel="preconnect" href="https://fonts.googleapis.com" />
        <link rel="preconnect" href="https://fonts.gstatic.com" crossOrigin="anonymous" />
        <link
          rel="stylesheet"
          href="https://fonts.googleapis.com/css2?family=Geist:wght@300;400;500;600&family=Geist+Mono:wght@400;500&display=swap"
        />
      </head>
      <body>
        <Script
          src={`https://www.googletagmanager.com/gtag/js?id=${GA_ID}`}
          strategy="afterInteractive"
        />
        <Script id="ga-init" strategy="afterInteractive">
          {`window.dataLayer = window.dataLayer || [];
function gtag(){dataLayer.push(arguments);}
gtag('js', new Date());
gtag('config', '${GA_ID}');`}
        </Script>
        <PcbGrid />
        <Nav />
        <main className="container">{children}</main>
        <Footer />
      </body>
    </html>
  );
}
