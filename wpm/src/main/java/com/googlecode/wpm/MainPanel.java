package com.googlecode.wpm;

import java.awt.Dialog.ModalityType;
import java.awt.Window;
import java.beans.PropertyChangeEvent;
import java.beans.PropertyChangeListener;
import java.io.IOException;
import java.net.URL;
import java.util.logging.Level;
import java.util.logging.Logger;
import javax.swing.DefaultListModel;
import javax.swing.JDialog;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.SwingConstants;
import javax.swing.SwingUtilities;
import javax.swing.SwingWorker;

/**
 * Main panel.
 */
public class MainPanel extends javax.swing.JPanel {
    /** 
     * Creates new form MainPanel
     */
    public MainPanel() {
        initComponents();
        jListValueChanged(null);
    }

    /** This method is called from within the constructor to
     * initialize the form.
     * WARNING: Do NOT modify this code. The content of this method is
     * always regenerated by the Form Editor.
     */
    @SuppressWarnings("unchecked")
    // <editor-fold defaultstate="collapsed" desc="Generated Code">//GEN-BEGIN:initComponents
    private void initComponents() {

        jScrollPane1 = new javax.swing.JScrollPane();
        jList = new javax.swing.JList();
        jPanel1 = new javax.swing.JPanel();
        jButtonUninstall = new javax.swing.JButton();
        jButtonInstall = new javax.swing.JButton();

        setLayout(new java.awt.BorderLayout());

        jList.addListSelectionListener(new javax.swing.event.ListSelectionListener() {
            public void valueChanged(javax.swing.event.ListSelectionEvent evt) {
                jListValueChanged(evt);
            }
        });
        jScrollPane1.setViewportView(jList);

        add(jScrollPane1, java.awt.BorderLayout.CENTER);

        jButtonUninstall.setText("Uninstall");
        jButtonUninstall.addActionListener(new java.awt.event.ActionListener() {
            public void actionPerformed(java.awt.event.ActionEvent evt) {
                jButtonUninstallActionPerformed(evt);
            }
        });

        jButtonInstall.setText("Install");
        jButtonInstall.addActionListener(new java.awt.event.ActionListener() {
            public void actionPerformed(java.awt.event.ActionEvent evt) {
                jButtonInstallActionPerformed(evt);
            }
        });

        javax.swing.GroupLayout jPanel1Layout = new javax.swing.GroupLayout(jPanel1);
        jPanel1.setLayout(jPanel1Layout);
        jPanel1Layout.setHorizontalGroup(
            jPanel1Layout.createParallelGroup(javax.swing.GroupLayout.Alignment.LEADING)
            .addGroup(jPanel1Layout.createSequentialGroup()
                .addContainerGap()
                .addComponent(jButtonInstall)
                .addPreferredGap(javax.swing.LayoutStyle.ComponentPlacement.RELATED)
                .addComponent(jButtonUninstall)
                .addGap(39, 39, 39))
        );
        jPanel1Layout.setVerticalGroup(
            jPanel1Layout.createParallelGroup(javax.swing.GroupLayout.Alignment.LEADING)
            .addGroup(jPanel1Layout.createSequentialGroup()
                .addContainerGap(javax.swing.GroupLayout.DEFAULT_SIZE, Short.MAX_VALUE)
                .addGroup(jPanel1Layout.createParallelGroup(javax.swing.GroupLayout.Alignment.BASELINE)
                    .addComponent(jButtonInstall)
                    .addComponent(jButtonUninstall)))
        );

        add(jPanel1, java.awt.BorderLayout.SOUTH);
    }// </editor-fold>//GEN-END:initComponents

    private void jListValueChanged(javax.swing.event.ListSelectionEvent evt) {//GEN-FIRST:event_jListValueChanged
        PackageVersion vp = (PackageVersion) jList.getSelectedValue();
        jButtonUninstall.setEnabled(vp != null && vp.isInstalled());
        jButtonInstall.setEnabled(vp != null && !vp.isInstalled());
    }//GEN-LAST:event_jListValueChanged

    private void jButtonInstallActionPerformed(java.awt.event.ActionEvent evt) {//GEN-FIRST:event_jButtonInstallActionPerformed
        block("Installing...", new JobRunnable() {
            @Override
            public void run(Job job) {
                job.setHint("Installing");
                PackageVersion pv = (PackageVersion) jList.getSelectedValue();
                try {
                    pv.install(job.createSubJob());
                } catch (IOException ex) {
                    App.rethrowAsRuntimeException(ex);
                }
                jList.repaint();
                jListValueChanged(null);
            }
        });
    }//GEN-LAST:event_jButtonInstallActionPerformed

    private void jButtonUninstallActionPerformed(java.awt.event.ActionEvent evt) {//GEN-FIRST:event_jButtonUninstallActionPerformed
        block("Uninstalling...", new JobRunnable() {
            @Override
            public void run(Job job) {
                PackageVersion pv = (PackageVersion) jList.getSelectedValue();
                try {
                    pv.uninstall();
                } catch (IOException ex) {
                    App.rethrowAsRuntimeException(ex);
                }
                jList.repaint();
                jListValueChanged(null);
            }
        });
    }//GEN-LAST:event_jButtonUninstallActionPerformed


    // Variables declaration - do not modify//GEN-BEGIN:variables
    private javax.swing.JButton jButtonInstall;
    private javax.swing.JButton jButtonUninstall;
    private javax.swing.JList jList;
    private javax.swing.JPanel jPanel1;
    private javax.swing.JScrollPane jScrollPane1;
    // End of variables declaration//GEN-END:variables

    /**
     * This should be called on the EDT.
     *
     * @param r a new repository or null
     */
    private void setRepository(Repository r) {
        DefaultListModel m = new DefaultListModel();
        if (r != null) {
            for (PackageVersion pv: r.packageVersions) {
                m.addElement(pv);
            }
        }
        jList.setModel(m);
    }

    /**
     * Starts a thread and loads the repository.
     */
    public void reloadRepository() {
        this.block("Loading package repository", new JobRunnable() {
            @Override
            public void run(Job job) {
                try {
                    final URL location = Repository.getLocation();
                    final Repository r;
                    if (location != null) {
                        r = Repository.load(location);
                        r.updateInstalledStatus();
                    } else {
                        r = null;
                    }
                    SwingUtilities.invokeLater(new Runnable() {
                        @Override
                        public void run() {
                            setRepository(r);
                        }
                    });
                } catch (Throwable ex) {
                    App.rethrowAsRuntimeException(ex);
                }
            }
        });
    }

    /**
     * Blocks the UI and runs the given Runnable.
     *
     * @param title name of the performed operation
     * @param r the operation
     */
    public void block(String title, final JobRunnable r) {
        final Window w = SwingUtilities.getWindowAncestor(this);
        final JDialog d = new JDialog(
                w, title, ModalityType.APPLICATION_MODAL);
        final JLabel jLabel = new JLabel("Working...");
        jLabel.setHorizontalAlignment(SwingConstants.CENTER);
        d.getContentPane().add(jLabel);
        d.setSize(400, 70);
        d.setDefaultCloseOperation(JFrame.DO_NOTHING_ON_CLOSE);
        d.setLocation(w.getX() + (w.getWidth() - d.getWidth()) / 2,
                w.getY() + (w.getHeight() - d.getHeight()) / 2);
        final Job job = new Job();
        job.addPropertyChangeListener(new PropertyChangeListener() {
            @Override
            public void propertyChange(PropertyChangeEvent evt) {
                jLabel.setText(job.getHint());
            }
        });
        new SwingWorker<Void, Void>() {
            @Override
            protected Void doInBackground() throws Exception {
                try {
                    r.run(job);
                } catch (final Throwable t) {
                    SwingUtilities.invokeLater(new Runnable() {
                        @Override
                        public void run() {
                            App.informUser(t);
                        }
                    });
                }
                return null;
            }

            @Override
            protected void done() {
                d.setVisible(false);
                d.dispose();
            }
        }.execute();
        d.setVisible(true);
    }
}
