(ns status-im.hardwallet.card
  (:require [re-frame.core :as re-frame]
            [status-im.react-native.js-dependencies :as js-dependencies]
            [status-im.utils.config :as config]
            [status-im.utils.platform :as platform]
            [taoensso.timbre :as log]))

(defonce keycard (.-default js-dependencies/status-keycard))
(defonce event-emitter (.-DeviceEventEmitter js-dependencies/react-native))

(defn check-nfc-support []
  (when config/hardwallet-enabled?
    (.. keycard
        nfcIsSupported
        (then #(re-frame/dispatch [:hardwallet.callback/check-nfc-support-success %])))))

(defn check-nfc-enabled []
  (when platform/android?
    (.. keycard
        nfcIsEnabled
        (then #(re-frame/dispatch [:hardwallet.callback/check-nfc-enabled-success %])))))

(defn open-nfc-settings []
  (when platform/android?
    (.openNfcSettings keycard)))

(defn start []
  (when config/hardwallet-enabled?
    (.. keycard
        start
        (then #(log/debug "[hardwallet] module started"))
        (catch #(log/debug "[hardwallet] module not started " %)))))

(defn initialize []
  (when config/hardwallet-enabled?
    (.. keycard
        init
        (then #(re-frame/dispatch [:hardwallet.callback/on-initialization-success %]))
        (catch #(re-frame/dispatch [:hardwallet.callback/on-initialization-error (str %)])))))

(defn install-applet-and-init-card []
  (when config/hardwallet-enabled?
    (.. keycard
        installAppletAndInitCard
        (then #(re-frame/dispatch [:hardwallet.callback/on-install-applet-and-init-card-success %]))
        (catch #(re-frame/dispatch [:hardwallet.callback/on-install-applet-and-init-card-error (str %)])))))

(defn remove-event-listeners []
  (doseq [event ["keyCardOnConnected" "keyCardOnDisconnected"]]
    (.removeAllListeners event-emitter event)))

(defn register-card-events []
  (when (and config/hardwallet-enabled?)

    (remove-event-listeners)

    (re-frame/dispatch [:hardwallet.callback/on-register-card-events
                        {:on-card-connected
                         (.addListener event-emitter
                                       "keyCardOnConnected"
                                       #(re-frame/dispatch [:hardwallet.callback/on-card-connected %]))

                         :on-card-disconnected
                         (.addListener event-emitter
                                       "keyCardOnDisconnected"
                                       #(re-frame/dispatch [:hardwallet.callback/on-card-disconnected %]))}])))

(defn pair
  [{:keys [password]}]
  (when password
    (.. keycard
        (pair password)
        (then #(re-frame/dispatch [:hardwallet.callback/on-pairing-success %]))
        (catch #(re-frame/dispatch [:hardwallet.callback/on-pairing-error (str %)])))))

(defn generate-mnemonic
  [{:keys [pairing]}]
  (when pairing
    (.. keycard
        (generateMnemonic pairing)
        (then #(re-frame/dispatch [:hardwallet.callback/on-generate-mnemonic-success %]))
        (catch #(re-frame/dispatch [:hardwallet.callback/on-generate-mnemonic-error (str %)])))))

(defn save-mnemonic
  [{:keys [mnemonic pairing pin]}]
  (when pairing
    (.. keycard
        (saveMnemonic mnemonic pairing pin)
        (then #(re-frame/dispatch [:hardwallet.callback/on-save-mnemonic-success %]))
        (catch #(re-frame/dispatch [:hardwallet.callback/on-save-mnemonic-error (str %)])))))

(defn generate-and-load-key
  [{:keys [mnemonic pairing pin]}]
  (when pairing
    (.. keycard
        (generateAndLoadKey mnemonic pairing pin)
        (then #(re-frame/dispatch [:hardwallet.callback/on-generate-and-load-key-success %]))
        (catch #(re-frame/dispatch [:hardwallet.callback/on-generate-and-load-key-error (str %)])))))

(defn get-application-info []
  (.. keycard
      getApplicationInfo
      (then #(re-frame/dispatch [:hardwallet.callback/on-get-application-info-success %]))
      (catch #(re-frame/dispatch [:hardwallet.callback/on-get-application-info-error (str %)]))))

(defn derive-key
  [{:keys [path pairing pin]}]
  (.. keycard
      (deriveKey path pairing pin)
      (then #(re-frame/dispatch [:hardwallet.callback/on-derive-key-success %]))
      (catch #(re-frame/dispatch [:hardwallet.callback/on-derive-key-success (str %)]))))

(defn export-key
  [{:keys [pairing pin]}]
  (.. keycard
      (exportKey pairing pin)
      (then #(re-frame/dispatch [:hardwallet.callback/on-export-key-success %]))
      (catch #(re-frame/dispatch [:hardwallet.callback/on-export-key-error (str %)]))))

(defn get-keys
  [{:keys [pairing pin]}]
  (.. keycard
      (getKeys pairing pin)
      (then #(re-frame/dispatch [:hardwallet.callback/on-get-keys-success %]))
      (catch #(re-frame/dispatch [:hardwallet.callback/on-get-keys-error (str %)]))))

(defn install-applet
  []
  (.. keycard
      installApplet
      (then #(re-frame/dispatch [:hardwallet.callback/on-install-applet-success %]))
      (catch #(re-frame/dispatch [:hardwallet.callback/on-install-applet-error (str %)]))))
