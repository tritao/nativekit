package nativekit.ui.style;

/** Value-level difference between two computed styles, classified by impact. */
class StyleDiff {
	public final changed:Bool;
	public final impact:StyleImpact;

	public function new(changed:Bool, impact:StyleImpact) {
		this.changed = changed;
		this.impact = impact;
	}

	/**
	 * Compares resolved values only. Provenance changes do not invalidate work
	 * when the resulting value is unchanged.
	 */
	public static function compare(previous:Null<ComputedStyle>, current:Null<ComputedStyle>):StyleDiff {
		if (previous == current)
			return new StyleDiff(false, StyleImpact.None);

		var changed = false;
		var impact:StyleImpact = StyleImpact.None;
		for (property in StyleProperty.all()) {
			var previousHas = previous != null && previous.has(property);
			var currentHas = current != null && current.has(property);
			if (previousHas == currentHas && (!previousHas ||
				property.isEqual(previous.get(property), current.get(property))))
				continue;
			changed = true;
			impact |= property.impact;
		}
		return new StyleDiff(changed, impact);
	}
}
