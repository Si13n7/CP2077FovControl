public native class FovControl extends IScriptable {
	public static native func IsPatchingAllowed() -> Bool
	public static native func PreventPatching() -> Bool
	public static native func ReleasePatching() -> Bool

	public static native func IsLocked() -> Bool
	public static native func Lock() -> Bool
	public static native func Unlock() -> Bool
	public static native func ToggleLock() -> Bool

	public static native func ConvertFormat(fov: Float, isSettingsFormat: Bool) -> Float
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
