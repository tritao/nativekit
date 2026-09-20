package nativekit.scene;

/** Reusable set of occurrences to highlight through a view material override. */
class SelectionSet {
	var entries:Array<Occurrence> = [];

	public function new() {}

	public function add(occurrence:Occurrence):SelectionSet {
		if (!contains(occurrence))
			entries.push(occurrence);
		return this;
	}

	public function remove(occurrence:Occurrence):SelectionSet {
		var index = 0;
		while (index < entries.length) {
			if (entries[index].equals(occurrence))
				entries.splice(index, 1);
			else
				index++;
		}
		return this;
	}

	public function clear():SelectionSet {
		entries.resize(0);
		return this;
	}

	public function contains(occurrence:Occurrence):Bool {
		for (value in entries)
			if (value.equals(occurrence))
				return true;
		return false;
	}

	public function count():Int
		return entries.length;

	@:allow(SceneView)
	function apply(view:SceneView, material:Material):Void {
		for (occurrence in entries)
			view.setSelectionMaterial(occurrence, material);
	}
}
