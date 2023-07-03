R"(
	class TDeluge {
		construct new() { _init = Fn.new {} }

		init() { _init.call() }
		onInit(fn) { _init = fn }

		foreign print(text)

		foreign pressButton(x,y,on)
		press(btn) {
			pressButton(btn[0], btn[1], true)
			pressButton(btn[0], btn[1], false)
		}
		/*
		mode=(val) {
			_mode = val
			mode_(_mode)
		}
		foreign mode_(val)
		*/
	}
	var Deluge = TDeluge.new()

	/*
	class Song {
	  foreign static load(name)
	}
	*/
)"
