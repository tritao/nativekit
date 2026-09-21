package nativekit.scene;

import NativeKitScene;

/** Owns one mutable NativeKit scene and its explicitly created resources. */
class Scene {
	final owner:Ownednkscene_scene;
	final resources:Array<Void->Void> = [];
	var disposed:Bool = false;

	private function new(owner:Ownednkscene_scene) {
		this.owner = owner;
	}

	public static function create():Scene {
		var made = NativeKitScene.nkscene_scene_create();
		check(made.status, "scene.create");
		return new Scene(made.out_scene);
	}

	public function nativeHandle():nkscene_scene {
		ensureLive();
		return owner.borrow();
	}

	public function beginTransaction():Transaction {
		ensureLive();
		var made = NativeKitScene.nkscene_transaction_begin(owner.borrow());
		check(made.status, "scene.beginTransaction");
		return new Transaction(this, made.out_transaction);
	}

	public function snapshot():Snapshot {
		ensureLive();
		var made = NativeKitScene.nkscene_scene_snapshot(owner.borrow());
		check(made.status, "scene.snapshot");
		return new Snapshot(made.out_snapshot);
	}

	public function createGeometry():Geometry {
		ensureLive();
		var made = NativeKitScene.nkscene_geometry_create(owner.borrow());
		check(made.status, "scene.createGeometry");
		return new Geometry(this, made.out_geometry);
	}

	public function createMaterial():Material {
		ensureLive();
		var made = NativeKitScene.nkscene_material_create(owner.borrow());
		check(made.status, "scene.createMaterial");
		return new Material(this, made.out_material);
	}

	public function createImage():Image {
		ensureLive();
		var made = NativeKitScene.nkscene_image_create(owner.borrow());
		check(made.status, "scene.createImage");
		return new Image(this, made.out_image);
	}

	public function createTexture():Texture {
		ensureLive();
		var made = NativeKitScene.nkscene_texture_create(owner.borrow());
		check(made.status, "scene.createTexture");
		return new Texture(this, made.out_texture);
	}

	public function createSampler():Sampler {
		ensureLive();
		var made = NativeKitScene.nkscene_sampler_create(owner.borrow());
		check(made.status, "scene.createSampler");
		return new Sampler(this, made.out_sampler);
	}

	public function createCamera():Camera {
		ensureLive();
		var made = NativeKitScene.nkscene_camera_create(owner.borrow());
		check(made.status, "scene.createCamera");
		return new Camera(this, made.out_camera);
	}

	public function createLight():Light {
		ensureLive();
		var made = NativeKitScene.nkscene_light_create(owner.borrow());
		check(made.status, "scene.createLight");
		return new Light(this, made.out_light);
	}

	public function setGeometryData(geometry:Geometry, data:GeometryData):Void {
		ensureLive();
		check(NativeKitScene.nkscene_geometry_set_data(owner.borrow(), geometry.id(), data.nativeValue()),
			"scene.setGeometryData");
	}

	public function setMaterialData(material:Material, data:MaterialData):Void {
		ensureLive();
		check(NativeKitScene.nkscene_material_set_data(owner.borrow(), material.id(), data.nativeValue()),
			"scene.setMaterialData");
	}

	public function setImageData(image:Image, data:ImageData):Void
		image.setData(data);

	public function setTextureData(texture:Texture, data:TextureData):Void
		texture.setData(data);

	public function setSamplerData(sampler:Sampler, data:SamplerData):Void
		sampler.setData(data);

	public function setCameraData(camera:Camera, data:CameraData):Void
		camera.setData(data);

	public function setLightData(light:Light, data:LightData):Void
		light.setData(data);

	/** Disposes child resources before releasing the scene handle. */
	public function dispose():Void {
		if (disposed)
			return;
		for (index in 0...resources.length)
			resources[resources.length - 1 - index]();
		owner.close();
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(Transaction, Geometry, Material, Image, Texture, Sampler, Camera, Light)
	function ensureLive():Void {
		if (disposed)
			throw "Scene has been disposed";
	}

	@:allow(Geometry, Material, Image, Texture, Sampler, Camera, Light)
	function registerResource(release:Void->Void):Void {
		ensureLive();
		resources.push(release);
	}

	static function check(status:Int, operation:String):Void {
		if (status != NativeKitSceneConstants.NKS_OK)
			throw '$operation failed with NativeKit scene status $status';
	}
}
