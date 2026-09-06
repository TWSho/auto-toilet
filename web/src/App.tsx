import ToiletCard from "./components/ToiletCard";
import LedCard from "./components/LedCard";
import BgmCard from "./components/BgmCard";
import SceneCard from "./components/SceneCard";
import StatusCard from "./components/StatusCard";
import { ToastProvider } from "./components/Toast";
import { BackendProvider } from "./context/BackendContext";

export default function App() {
  return (
    <ToastProvider>
      <BackendProvider>
        <div className="app">
          <main className="columns">
            <ToiletCard />
            <div className="col-stack">
              <LedCard />
              <BgmCard />
            </div>
            <div className="col-stack col-stack--scene">
              <StatusCard />
              <SceneCard />
            </div>
          </main>
        </div>
      </BackendProvider>
    </ToastProvider>
  );
}
