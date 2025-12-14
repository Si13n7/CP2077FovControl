public native class FovControl extends IScriptable {
	public static native func IsPatchingAllowed() -> Bool
	public static native func PreventPatching() -> Bool
	public static native func ReleasePatching() -> Bool

	public static native func IsLocked() -> Bool
	public static native func Lock() -> Bool
	public static native func Unlock() -> Bool
	public static native func ToggleLock() -> Bool

	public static native func ConvertFormat(fov: Float, isSettingsFormat: Bool) -> Float

	public static native func Version() -> String
}

@addMethod(CameraComponent)
public func GetDisplayFOV() -> Float {
	return FovControl.ConvertFormat(this.GetFOV(), false);
}

@addMethod(CameraComponent)
public func SetDisplayFOV(displayFov: Float) -> Void {
	let value = FovControl.ConvertFormat(displayFov, true);
	if value > 0.3 && value < 172.2 {
		this.SetFOV(value);
	}
}

@if(ModuleExists("Codeware"))
@addField(PlayerPuppet)
private let m_tppCamComp: wref<vehicleTPPCameraComponent>;

@if(ModuleExists("Codeware"))
@addMethod(PlayerPuppet)
public func GetTPPCameraComponent() -> wref<vehicleTPPCameraComponent>
{
	if IsDefined(this.m_tppCamComp) {
		return this.m_tppCamComp;
	}
	let comps = this.GetComponents();
	for comp in comps {
		let tpp = comp as vehicleTPPCameraComponent;
		if IsDefined(tpp) {
			this.m_tppCamComp = tpp;
			return tpp;
		}
	}
	return null;
}

@if(ModuleExists("Codeware"))
@addMethod(VehicleComponent)
public func GetCameraComponent() -> wref<vehicleTPPCameraComponent> {
	let player = GetPlayer(this.GetVehicle().GetGame());
	if !IsDefined(player) {
		return null;
	}
	return player.GetTPPCameraComponent();
}

@if(!ModuleExists("Codeware"))
@addMethod(PlayerPuppet)
public func GetTPPCameraComponent() -> wref<vehicleTPPCameraComponent> { return null; }

@if(!ModuleExists("Codeware"))
@addMethod(VehicleComponent)
public func GetCameraComponent() -> wref<vehicleTPPCameraComponent> { return null; }

public class FovControlSchedulerTick extends DelayCallback {
	public let m_scheduler: ref<FovControlScheduler>;

	public func Call() -> Void {
		if IsDefined(this.m_scheduler) {
			this.m_scheduler.Tick();
		}
	}
}

public class FovControlScheduler extends IScriptable {
	private let m_camComp: wref<CameraComponent>;
	private let m_lastFov: Float;
	private let m_active: Bool;
	private let m_callback: ref<FovControlSchedulerTick>;

	public func IsActive() -> Bool {
		return this.m_active;
	}

	public func Start(camComp: wref<CameraComponent>) -> Bool {
		if !IsDefined(camComp) {
			return false;
		}

		if !FovControl.IsPatchingAllowed() || !FovControl.IsLocked() {
			return false;
		}

		this.m_camComp = camComp;
		this.m_active = true;
		this.m_lastFov = -1.0;

		if !IsDefined(this.m_callback) {
			this.m_callback = new FovControlSchedulerTick();
		}
		this.m_callback.m_scheduler = this;

		if !FovControl.Unlock() {
			this.Stop();
			return false;
		}

		this.Schedule();
		return true;
	}

	private func Stop() -> Void {
		this.m_active = false;
		if IsDefined(this.m_callback) {
			this.m_callback.m_scheduler = null;
		}
	}

	private func Schedule() -> Void {
		if !this.m_active {
			return;
		}

		let ds = GameInstance.GetDelaySystem(GetGameInstance());
		if !IsDefined(ds) {
			this.Stop();
			return;
		}

		ds.DelayCallback(this.m_callback, 0.3, false);
	}

	public func Tick() -> Void {
		if !this.m_active {
			return;
		}

		if !IsDefined(this.m_camComp) || !FovControl.IsPatchingAllowed() {
			this.Stop();
			return;
		}

		let current: Float = this.m_camComp.GetFOV();
		if AbsF(current - this.m_lastFov) > 0.00001 {
			this.m_lastFov = current;
			this.Schedule();
			return;
		}

		if !FovControl.Lock() {
			return;
		}

		this.Stop();
	}
}

@addField(CameraComponent)
private let m_fovControlScheduler: ref<FovControlScheduler>;

@addMethod(CameraComponent)
public func PendingSetFOV() -> Bool {
	if IsDefined(this.m_fovControlScheduler) && this.m_fovControlScheduler.IsActive() {
		return false;
	}

	this.m_fovControlScheduler = new FovControlScheduler();
	return this.m_fovControlScheduler.Start(this);
}
