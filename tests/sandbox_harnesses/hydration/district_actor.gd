extends AnimatableBody3D

static var created := 0
static var destroyed := 0
var velocity := Vector3.ZERO


func _init() -> void:
	created += 1


func _notification(what: int) -> void:
	if what == NOTIFICATION_PREDELETE:
		destroyed += 1


func _ready() -> void:
	$Hydratable.hydrated.connect(_on_hydrated)


func _on_hydrated(_entity: RID, state: Dictionary) -> void:
	velocity = state.velocity


func _physics_process(delta: float) -> void:
	global_position += velocity * delta
