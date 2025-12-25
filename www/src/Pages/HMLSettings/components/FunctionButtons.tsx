import { useNavigate } from 'react-router-dom';
import { Button } from 'react-bootstrap';

import Section from '../../../Components/Section';

export default function FunctionButtons() {
	const navigate = useNavigate();

	const handleHotkeySettings = () => {
		// Navigate to settings page with hotkey tab
		navigate('/settings', { state: { activeTab: 'hotkey' } });
	};

	const handleMacroSettings = () => {
		// Navigate to macro configuration page
		navigate('/macro');
	};

	return (
		<div>
			<Section title="功能按键">
			<div style={{ paddingLeft: '100px' }}>
				<div style={{ display: 'flex', flexDirection: 'column', gap: '12px', alignItems: 'flex-start' }}>
					<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
						<Button
							variant="primary"
							onClick={handleHotkeySettings}
							style={{ minWidth: '120px' }}
						>
							热键设置
						</Button>
						<span className="text-muted">
							热键需要在将某个按键设为FN键以实现热键功能。
						</span>
					</div>
					<Button
						variant="primary"
						onClick={handleMacroSettings}
						style={{ minWidth: '120px' }}
					>
						宏设置
					</Button>
				</div>
			</div>
		</Section>
		</div>
	);
}

