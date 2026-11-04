# OpenClient integration — current task 2026-11-04

The user authorized merging Beta's Null features/profile separation into the
updated regular OpenKRX in this tree, renaming its launcher to OpenClient,
creating/pushing eqsit/openclient and publishing Windows+Linux artifacts.
Use updated KRX60a7dee as base; preserve Basic/Legit/Pilot/AutoEdge/TAS updates.
Null settings factory defaults match original; do not import a personal Null
config or overwrite the user's saved tuning. Beta remains separate.
Keep README short. This task explicitly authorizes commit/push/release; no
ChatGPT Web or VM/Windows runtime comparison. Work directly and build locally.

Earlier project history follows; old keep-main-unchanged restrictions applied
before this explicit merge task and are superseded for this operation.

# Текущее основное дерево OpenKRX — после переноса 2026-11-04

По явному запросу пользователя OpenKRX-new стал обычным OpenKRX; v0.1.9
опубликован на GitHub. Работать в `/home/a/openkrx`, читать верх HANDOFF.md.
Текущая Linux-сборка `/home/a/openkrx-build/build` настроена прямо на это
дерево; Windows cross-build — `/home/a/openkrx-build/build-win`.
Отдельные new-дерево и сборка сохранены в
`/home/a/backups/openkrx-new-merged-20261104/{tree,build}` как архив.
Обычный ярлык запускает `scripts/openkrx.sh` и личный профиль DDNet;
бету `/home/a/openkrx-beta`, её ярлыки и профиль не менять.
Прямые локальные правки; Web orchestration отключён. Задача включает
все агенты Basic/Legit/Blatant/Fent/Pilot. analysis Evidence и свои native тесты
отличать от отсутствующего original runtime A/B. Публикация main/v0.1.9
авторизована пользователем; новые публикации требуют соответствующего запроса.

# Работа над OpenKRX

- Сначала прочитать `HANDOFF.md`, затем актуальный конец `JOURNAL.md` и
  `research/runtime/FINDINGS.md`.
- Цель пользователя: восстановить Avoid Freeze / Blatant из предоставленного
  Goodcrack/KRX с точным поведением и UI, а не выдать похожую эвристику за копию.
- Основной проект, реверс, тесты, результаты и итоговый ярлык — `/home/a/openkrx`
  на этой рабочей станции. Пользователь перенёс сессию на машину сборки:
  собирать локально в `/home/a/openkrx-build/build`, без `ssh home`.
  Порядок синхронизации исходников и сборки — в проектном скилле.
- Все существенные находки, ошибки, проверки и неизвестные записывать в
  `JOURNAL.md`. Отличать analysis Evidence от shell/QMP-наблюдений и гипотез.
- Исходный анализ уже сохранён. Не начинать весь реверс заново, не считать
  декомпиляцию доказанным поведенческим совпадением.
- Пользователь требует analysis для анализа эталонного GoodCrack/KRX.
  Пользовательские дополнения (например, возврат оружия после Rocket Save)
  отличать от перенесённого поведения оригинала.
- Пока нет подтверждённого результата, не называть dist-клиент готовой копией.
- Реализовывать изменения напрямую доступными локальными инструментами.
  Не запускать chatgpt-web-orchestrator или ZIP pipeline без нового явного запроса.
- По последнему указанию пользователя ВМ не использовать. Дополнительные
  запуски и сравнения Windows с Linux не проводить. Ранее полученные Evidence
  сохраняются; историческое состояние ВМ — в runtime/FINDINGS.md.
- Ярлык создать на **этой** машине после получения и проверки итоговой сборки.
- История коммитов уже есть; смотреть фактический git status/log. Не удалять
  untracked исследовательские материалы и код. Текущие правки не коммитить и
  релизы не публиковать без запроса пользователя.
- Поддерживать HANDOFF.md как текущую сводку; JOURNAL.md хранит историю.
  Проектный скилл: /home/a/.codex/skills/openkrx-project/SKILL.md.
