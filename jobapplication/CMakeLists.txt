# Neither a dock nor a source: the paper is an item VTube Studio draws on the model, so the plugin is the toggle, its
# hotkey, what chat writes on it and its settings. Built from the shared chat trigger (sarxina-chat-trigger) and its
# own sources. Chat, VTube Studio and Twitch's API come from the Sarxina OBS Plugin Manager.
sarxina_add_obs_module(
  jobapplication
  TITLE JobApplication
  VERSION 1.2.0
  DESCRIPTION "JobApplication plugin for OBS Studio"
  SOURCES
    src/form.cpp
    src/form.hpp
    src/jobapplication.cpp
    src/jobapplication.hpp
    src/moc3.cpp
    src/moc3.hpp
    src/paper.cpp
    src/paper.hpp
    src/paper-item.cpp
    src/paper-item.hpp
    src/paper-prop.cpp
    src/paper-prop.hpp
    src/self-test.cpp
  MODULE_SOURCES
    src/in-obs-check.cpp
    src/plugin-main.cpp
    src/test-route.cpp
  LIBRARIES sarxina-chat-trigger
)
