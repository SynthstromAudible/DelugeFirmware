R"(
	class TDeluge {
		construct new() {
			_init = Fn.new {}
			_buttonHooks = {}
		}

		init() { _init.call() }
		onInit(fn) { _init = fn }

		bind(btn, fn) {
			if (btn is Button) {
				_buttonHooks[btn.index] = fn
			}
		}
		buttonAction(index,down) {
			if (_buttonHooks.containsKey(index)) {
				_buttonHooks[index].call(down)
			}
		}

		foreign print(text)

		foreign pressButton(btn,on)
		press(btn) {
			pressButton(btn, true)
			pressButton(btn, false)
		}
	}
	var Deluge = TDeluge.new()
)"
