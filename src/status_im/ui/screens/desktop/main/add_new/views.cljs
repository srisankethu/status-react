(ns status-im.ui.screens.desktop.main.add-new.views
  (:require-macros [status-im.utils.views :as views])
  (:require [status-im.ui.components.icons.vector-icons :as icons]
            [status-im.ui.screens.add-new.new-public-chat.view :as public-chat]
            [status-im.ui.components.list.views :as list]
            [clojure.string :as string]
            [status-im.i18n :as i18n]
            [re-frame.core :as re-frame]
            [status-im.ui.screens.desktop.main.add-new.styles :as styles]
            [status-im.ui.screens.add-new.new-public-chat.view :refer [default-public-chats]]
            [status-im.ui.screens.add-new.new-public-chat.db :as public-chat-db]
            [taoensso.timbre :as log]
            [status-im.ui.components.react :as react]
            [status-im.ui.components.colors :as colors]))

(views/defview error-tooltip [text]
  [react/view {:style styles/tooltip-container}
   [react/view {:style styles/tooltip-icon-text}
    [react/text {:style {:font-size 14 :color colors/red}}
     text]]
   [react/view {:style styles/tooltip-triangle}]])

(views/defview new-contact []
  (views/letsubs [new-contact-identity [:get :contacts/new-identity]
                  contacts             [:all-added-people-contacts]
                  chat-error           [:new-identity-error]
                  topic                [:get :public-group-topic]
                  topic-error          [:new-topic-error-message]
                  account              [:get-current-account]
                  topic-input-ref      (atom nil)]
    {:component-will-unmount #(re-frame/dispatch [:new-chat/set-new-identity nil])}
    [react/scroll-view
     [react/view {:style styles/new-contact-view}
      ^{:key "newcontact"}
      [react/view {:style styles/new-contact-title}
       [react/text {:style styles/new-contact-title-text
                    :font  :medium}
        (i18n/label :new-chat)]]
      [react/text {:style styles/new-contact-subtitle} (i18n/label :contact-code)]
      [react/view {:style styles/new-contact-separator}]
      (let [disable? (or (not (string/blank? chat-error))
                         (string/blank? new-contact-identity))]
        [react/view {:style styles/add-contact-edit-view}
         [react/view {:flex 1}
          (when (and chat-error (not (string/blank? new-contact-identity)))
            [error-tooltip chat-error])
          [react/text-input {:placeholder      "0x..."
                             :flex             1
                             :style            styles/add-contact-input
                             :selection-color  colors/hawkes-blue
                             :font             :default
                             :on-change        (fn [e]
                                                 (let [native-event (.-nativeEvent e)
                                                       text (.-text native-event)]
                                                   (re-frame/dispatch [:new-chat/set-new-identity text])))}]]
         [react/touchable-highlight {:disabled disable? :on-press #(re-frame/dispatch [:add-contact-handler new-contact-identity])}
          [react/view
           {:style (styles/add-contact-button disable?)}
           [react/text
            {:style (styles/add-contact-button-text disable?)}
            (i18n/label :start-chat)]]]])
      ^{:key "choosecontact"}
      [react/view
       (when (seq contacts) [react/text {:style styles/new-contact-subtitle} (i18n/label :or-choose-a-contact)])
       [react/view {:style styles/suggested-contacts}
        (doall
         (for [c contacts]
           ^{:key (:whisper-identity c)}
           [react/touchable-highlight {:on-press #(do
                                                    (re-frame/dispatch [:set :contacts/new-identity (:whisper-identity c)])
                                                    (re-frame/dispatch [:add-contact-handler (:whisper-identity c)]))}
            [react/view {:style styles/suggested-contact-view}
             [react/image {:style  styles/suggested-contact-image
                           :source {:uri (:photo-path c)}}]
             [react/text {:style styles/new-contact-subtitle} (:name c)]]]))]]
      ^{:key "publicchat"}
      [react/view {:style styles/new-contact-title}
       [react/text {:style styles/new-contact-title-text
                    :font  :medium}
        (i18n/label :new-public-group-chat)]]
      [react/text {:style styles/new-contact-subtitle} (i18n/label :public-group-topic)]
      [react/view {:style styles/new-contact-separator}]
      (let [disable? (or (not (string/blank? topic-error))
                         (string/blank? topic))]
        [react/view {:style styles/add-contact-edit-view}
         [react/view {:flex 1}
          (when (and topic-error (not (string/blank? topic)))
            [error-tooltip topic-error])
          [react/text-input {:flex             1
                             :ref              #(when (and (nil? @topic-input-ref) %)
                                                  (.setNativeProps % (js-obj "text" "#"))
                                                  (reset! topic-input-ref %))
                             :style            styles/add-contact-input
                             :font             :default
                             :selection-color  colors/hawkes-blue
                             :on-change        (fn [e]
                                                 (let [native-event (.-nativeEvent e)
                                                       text (.-text native-event)
                                                       [_ before after] (first (re-seq #"(.*)\#(.*)" text))]
                                                   (.setNativeProps @topic-input-ref (js-obj "text" (str "#" before after)))
                                                   (re-frame/dispatch [:set :public-group-topic (subs text 1)])))}]]
         [react/touchable-highlight {:disabled disable?
                                     :on-press #(do
                                                  (re-frame/dispatch [:set :public-group-topic nil])
                                                  (re-frame/dispatch [:create-new-public-chat topic]))}
          [react/view {:style (styles/add-contact-button disable?)}
           [react/text {:style (styles/add-contact-button-text disable?)}
            (i18n/label :new-public-group-chat)]]]])
      [react/text {:style styles/new-contact-subtitle} (i18n/label :selected-for-you)]
      [react/view {:style styles/suggested-contacts}
       (doall
        (for [topic public-chat/default-public-chats]
          ^{:key topic}
          [react/touchable-highlight {:on-press #(do
                                                   (re-frame/dispatch [:set :public-group-topic nil])
                                                   (re-frame/dispatch [:create-new-public-chat topic]))}
           [react/view {:style styles/suggested-contact-view}
            [react/view {:style styles/suggested-topic-image}
             [react/text {:style styles/suggested-topic-text} (string/capitalize (first topic))]]
            [react/text {:style styles/new-contact-subtitle} topic]]]))]]]))
