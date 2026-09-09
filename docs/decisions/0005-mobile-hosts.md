# ADR 0005: Caller-owned mobile hosts

Mobile platforms own their top-level application surfaces and lifecycle.
NativeKit therefore does not map `nk_window_create()` onto an Android Activity
or iOS view controller. A host application attaches a caller-owned native
container with `nk_mobile_host_attach()` and may create NativeKit child WebViews
inside the returned handle.

The host handle retains only the platform reference required to attach children.
It does not own the Activity, view controller, or container. Destroying a host
destroys its NativeKit children before releasing that reference. Lifecycle
changes are explicitly forwarded by the host application and never inferred
from desktop window state.

Platform objects cross the C ABI only during attachment. Android supplies a
`JNIEnv*` and `ViewGroup` object; the backend immediately creates a JNI global
reference. Future UIKit support will use a borrowed `UIView*` under the same
ownership contract.
