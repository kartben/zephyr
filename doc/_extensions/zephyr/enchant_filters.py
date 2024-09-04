from enchant.tokenize import Filter


class CustomFilter(Filter):
    def _skip(self, word):
        # ignore anything that looks like a board identifier
        print(word)
        return word in {
            "nrf",
            "stm",
            "stm32",
            "nucleo",
            "nucleo_f401re",
            "nucleo_f411re",
        }
