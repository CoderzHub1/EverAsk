import Image from "next/image";
import { Geist, Geist_Mono } from "next/font/google";
import { useEffect, useState, useRef } from "react";

const geistSans = Geist({
  variable: "--font-geist-sans",
  subsets: ["latin"],
});

const geistMono = Geist_Mono({
  variable: "--font-geist-mono",
  subsets: ["latin"],
});

export default function Home() {
  const [status, setStatus] = useState({
    listening_for_prompt: false,
    accumulated_prompt: "",
    last_transcript: "",
    latest_tts_text: ""
  });
  const [isPlaying, setIsPlaying] = useState(false);
  const audioRef = useRef(null);

  // Poll for status updates
  useEffect(() => {
    const pollStatus = async () => {
      try {
        const response = await fetch('http://localhost:5000/status');
        const data = await response.json();
        setStatus(data);

        // If there's new TTS text, play it
        if (data.latest_tts_text && data.latest_tts_text !== status.latest_tts_text) {
          await playTTS(data.latest_tts_text);
        }
      } catch (error) {
        console.error('Error fetching status:', error);
      }
    };

    const interval = setInterval(pollStatus, 1000); // Poll every second
    pollStatus(); // Initial poll

    return () => clearInterval(interval);
  }, [status.latest_tts_text]);

  const playTTS = async (text) => {
    if (isPlaying) return; // Prevent multiple simultaneous plays

    try {
      setIsPlaying(true);
      
      // Encode the text for URL
      const encodedText = encodeURIComponent(text);
      const ttsUrl = `http://localhost:5000/tts?text=${encodedText}`;
      
      // Create and play audio
      if (audioRef.current) {
        audioRef.current.src = ttsUrl;
        await audioRef.current.play();
      }

      // Clear the TTS text on backend after playing
      await fetch('http://localhost:5000/clear_tts');
      
    } catch (error) {
      console.error('Error playing TTS:', error);
    } finally {
      setIsPlaying(false);
    }
  };

  const getStatusColor = () => {
    if (status.listening_for_prompt) {
      return "text-yellow-600 dark:text-yellow-400";
    }
    return "text-green-600 dark:text-green-400";
  };

  const getStatusText = () => {
    if (status.listening_for_prompt) {
      return "🎙️ Listening for prompt...";
    }
    return "🤖 Ready for 'Ever Ask'";
  };

  return (
    <div
      className={`${geistSans.className} ${geistMono.className} flex min-h-screen items-center justify-center bg-zinc-50 font-sans dark:bg-black`}
    >
      {/* Hidden audio element */}
      <audio
        ref={audioRef}
        onEnded={() => setIsPlaying(false)}
        onError={() => setIsPlaying(false)}
      />

      <main className="flex min-h-screen w-full max-w-3xl flex-col items-center justify-between py-16 px-16 bg-white dark:bg-black sm:items-start">
        <Image
          className="dark:invert"
          src="/next.svg"
          alt="Next.js logo"
          width={100}
          height={20}
          priority
        />
        
        <div className="flex flex-col items-center gap-8 text-center sm:items-start sm:text-left w-full">
          <h1 className="max-w-xs text-4xl font-semibold leading-10 tracking-tight text-black dark:text-zinc-50">
            EverAsk Assistant
          </h1>
          
          {/* Status Display */}
          <div className="w-full max-w-md p-6 bg-gray-50 dark:bg-gray-900 rounded-lg">
            <h2 className="text-xl font-medium mb-4 text-black dark:text-white">System Status</h2>
            
            <div className="space-y-3">
              <div className={`text-lg font-medium ${getStatusColor()}`}>
                {getStatusText()}
              </div>
              
              {status.accumulated_prompt && (
                <div className="text-sm">
                  <span className="font-medium text-gray-700 dark:text-gray-300">Prompt: </span>
                  <span className="text-gray-600 dark:text-gray-400">"{status.accumulated_prompt}"</span>
                </div>
              )}
              
              {status.last_transcript && (
                <div className="text-sm">
                  <span className="font-medium text-gray-700 dark:text-gray-300">Last heard: </span>
                  <span className="text-gray-600 dark:text-gray-400">"{status.last_transcript}"</span>
                </div>
              )}
              
              {isPlaying && (
                <div className="text-sm text-blue-600 dark:text-blue-400 font-medium">
                  🔊 Playing response...
                </div>
              )}
            </div>
          </div>

          <div className="text-sm text-gray-500 dark:text-gray-400 max-w-md">
            <p className="mb-2">
              <strong>How to use:</strong>
            </p>
            <ol className="list-decimal list-inside space-y-1">
              <li>Say "Ever Ask" to start</li>
              <li>Speak your question or prompt</li>
              <li>Stay silent to submit</li>
              <li>Listen to the AI response</li>
            </ol>
          </div>
        </div>

        <div className="flex flex-col gap-4 text-base font-medium sm:flex-row">
          <div className="flex h-12 w-full items-center justify-center gap-2 rounded-full bg-foreground px-5 text-background md:w-[200px]">
            <span>Backend: Port 5000</span>
          </div>
          <div className="flex h-12 w-full items-center justify-center rounded-full border border-solid border-black/[.08] px-5 dark:border-white/[.145] md:w-[200px]">
            <span>Frontend: Port 3000</span>
          </div>
        </div>
      </main>
    </div>
  );
}
