import { ChevronDownIcon } from "./Icons";

interface DetailToggleProps {
  open: boolean;
  onToggle: () => void;
}

export default function DetailToggle({ open, onToggle }: DetailToggleProps) {
  return (
    <button type="button" className="detail-toggle" aria-expanded={open} onClick={onToggle}>
      詳細
      <span className={`chev ${open ? "open" : ""}`}>
        <ChevronDownIcon />
      </span>
    </button>
  );
}
